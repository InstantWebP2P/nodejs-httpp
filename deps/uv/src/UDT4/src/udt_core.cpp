/*****************************************************************************
Copyright (c) 2001 - 2011, The Board of Trustees of the University of Illinois.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the
  above copyright notice, this list of conditions
  and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the University of Illinois
  nor the names of its contributors may be used to
  endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

/*****************************************************************************
written by
   Yunhong Gu, last updated 02/28/2012
*****************************************************************************/

#ifndef WIN32
   #include <unistd.h>
   #include <netdb.h>
   #include <arpa/inet.h>
   #include <cerrno>
   #include <cstring>
   #include <cstdlib>
   #include <sys/ioctl.h>
#else
   #include <winsock2.h>
   #include <ws2tcpip.h>
   #ifdef LEGACY_WIN32
      #include <wspiapi.h>
   #endif
#endif

#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <cstring>
#include <iostream>
#include <cmath>
#include <sstream>
#include "queue.h"
#include "core.h"

using namespace std;


CUDTUnited CUDT::s_UDTUnited;

const UDTSOCKET CUDT::INVALID_SOCK = -1;
const int CUDT::ERROR = -1;

const UDTSOCKET UDT::INVALID_SOCK = CUDT::INVALID_SOCK;
const int UDT::ERROR = CUDT::ERROR;

const int32_t CSeqNo::m_iSeqNoTH     = 0x1FFFFFFF; // 0x3FFFFFFF -> 0x1FFFFFFF to support authentication on data packet
const int32_t CSeqNo::m_iMaxSeqNo    = 0x3FFFFFFF; // 0x7FFFFFFF -> 0x3FFFFFFF to support authentication on data packet
const int32_t CAckNo::m_iMaxAckSeqNo = 0x3FFFFFFF; // 0x7FFFFFFF -> 0x3FFFFFFF to support authentication on data packet
const int32_t CMsgNo::m_iMsgNoTH     =  0xFFFFFFF;
const int32_t CMsgNo::m_iMaxMsgNo    = 0x1FFFFFFF;

///const int CUDT::m_iVersion = 4;
///const int CUDT::m_iVersion = 6;
const int CUDT::m_iVersion = 7; // 7: support authentication on both control and data packet
                                // 6: support authentication on control packet only
                                // 4: no authenticaton support
const int CUDT::m_iSYNInterval = 10000;
const int CUDT::m_iSelfClockInterval = 64;

// create Osfd pair
#ifdef EVPIPE_OSFD
static void _createOsfd(SYSSOCKET m_evPipe[])
{
#ifndef WIN32
	// create event pipe with socketpair
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, m_evPipe)) {
            perror("socketpair creation failure");
            assert(0);
        };
	assert((m_evPipe[0] > 0) && (m_evPipe[1] > 0));
	// set event pipe non-block
	int flags = 0, rc = 0;

	fcntl(m_evPipe[0], F_SETFD, FD_CLOEXEC);
	flags = fcntl(m_evPipe[0], F_GETFL, 0);
	if (flags == -1) {
		flags = 0;
	}
	rc = fcntl(m_evPipe[0], F_SETFL, flags | O_NONBLOCK); assert(rc != -1);

	fcntl(m_evPipe[1], F_SETFD, FD_CLOEXEC);
	flags = fcntl(m_evPipe[1], F_GETFL, 0);
	if (flags == -1) {
		flags = 0;
	}
	rc = fcntl(m_evPipe[1], F_SETFL, flags | O_NONBLOCK); assert(rc != -1);
#else
	// create tcp pair as event pipe

	// This function has to be in a system-wide critical section so that
	// two instances of the library don't accidentally create signaler
	// crossing the process boundary.
	// We'll use named event object to implement the critical section.
	HANDLE sync = CreateEvent (NULL, FALSE, TRUE, "udt-evpipe-sync");
	assert(sync != NULL);

	// Enter the critical section.
	DWORD dwrc = WaitForSingleObject(sync, INFINITE);
	assert(dwrc == WAIT_OBJECT_0);

	// Windows has no 'socketpair' function. CreatePipe is no good as pipe
	// handles cannot be polled on. Here we create the socketpair by hand.
	m_evPipe[0] = INVALID_SOCKET;
	m_evPipe[1] = INVALID_SOCKET;

	// Create listening socket.
	SOCKET listener;
	listener = socket(AF_INET, SOCK_STREAM, 0);
	assert(listener != INVALID_SOCKET);

	// Set SO_REUSEADDR and TCP_NODELAY on listening socket.
	BOOL so_reuseaddr = 1;
	int rc = setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
			(char *)&so_reuseaddr, sizeof (so_reuseaddr));
	assert(rc != SOCKET_ERROR);
	BOOL tcp_nodelay = 1;
	rc = setsockopt(listener, IPPROTO_TCP, TCP_NODELAY,
			(char *)&tcp_nodelay, sizeof (tcp_nodelay));
	assert(rc != SOCKET_ERROR);

	// Bind listening socket to any free local port.
	// in case failed, we will re-try on the different port. :)
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof (addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	int lp = 56868; // start port to try
	int re_try = 0;
	while (1) {
		addr.sin_port = htons(lp);
		rc = bind(listener, (const struct sockaddr*) &addr, sizeof (addr));

		// ongoing next port
		if (re_try > 33) break;
		lp ++;
		re_try ++;

		if (rc == SOCKET_ERROR) continue;
		// Listen for incoming connections.
		rc = listen(listener, 1);
		if (rc == SOCKET_ERROR) {
			continue;
		} else {
			break;
		}
		Sleep(33);
	}
	if (re_try > 33) {
		assert(0);
	}

	// Create the writer socket.
	m_evPipe[1] = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0,  0);
	assert(m_evPipe[1] != INVALID_SOCKET);

	// Set TCP_NODELAY on writer socket.
	rc = setsockopt(m_evPipe[1], IPPROTO_TCP, TCP_NODELAY,
			(char *)&tcp_nodelay, sizeof (tcp_nodelay));
	assert (rc != SOCKET_ERROR);

	// Connect writer to the listener.
	rc = connect(m_evPipe[1], (sockaddr *) &addr, sizeof (addr));
	assert (rc != SOCKET_ERROR);

	// Accept connection from writer.
	m_evPipe[0] = accept(listener, NULL, NULL);
	assert (m_evPipe[0] != INVALID_SOCKET);

	// We don't need the listening socket anymore. Close it.
	rc = closesocket(listener);
	assert (rc != SOCKET_ERROR);

	// Exit the critical section.
	BOOL brc = SetEvent(sync);
	assert(brc != 0);

	// set event pipe non-block
	unsigned long arg = 1;
	rc = ioctlsocket(m_evPipe[0], FIONBIO, &arg); assert(rc != SOCKET_ERROR);
	rc = ioctlsocket(m_evPipe[1], FIONBIO, &arg); assert(rc != SOCKET_ERROR);
#endif
	///printf("open evPipe fds:%d,%d\n", m_evPipe[0], m_evPipe[1]);
}
#endif // Osfd

CUDT::CUDT()
{
   m_pSndBuffer = NULL;
   m_pRcvBuffer = NULL;
   m_pSndLossList = NULL;
   m_pRcvLossList = NULL;
   m_pACKWindow = NULL;
   m_pSndTimeWindow = NULL;
   m_pRcvTimeWindow = NULL;

   m_pSndQueue = NULL;
   m_pRcvQueue = NULL;
   m_pPeerAddr = NULL;
   m_pSNode = NULL;
   m_pRNode = NULL;

   // Connection cookie, peer IP changed count
   m_pCookie = -1;
   m_pPeerChanged = 0;

   // Initilize mutex and condition variables
   initSynch();

   // Default UDT configurations
  
   // !!! fixed iMSS to 1492 for RPI, need to root cause /////////////
   ///m_iMSS = 1500;
   ///m_iMSS = 1492;
   // !!! fixed iMSS to 1480 for MTU of some router, TBD...
   m_iMSS = 1480;
   ////////////////////////////////////////////////////////////////////  
 
   m_bSynSending = true;
   m_bSynRecving = true;
   m_iFlightFlagSize = 25600;
   m_iSndBufSize = 8192;
   m_iRcvBufSize = 8192; //Rcv buffer MUST NOT be bigger than Flight Flag size
   m_Linger.l_onoff = 1;
   m_Linger.l_linger = 180;
   m_iUDPSndBufSize = 65536;
   m_iUDPRcvBufSize = m_iRcvBufSize * m_iMSS;
   m_iSockType = UDT_STREAM;
   m_iIPversion = AF_INET;
   m_bRendezvous = false;
   m_iSndTimeOut = -1;
   m_iRcvTimeOut = -1;
   m_bReuseAddr = true; // always reuse existing Mux in default for connect socket
   m_bReuseAble = true; // alwasy allow Mux reusable and sharable
   m_llMaxBW = -1;
   m_iQos = 0;

   m_pCCFactory = new CCCFactory<CUDTCC>;
   m_pCC = NULL;
   m_pCache = NULL;

   // Initial status
   m_bOpened = false;
   m_bListening = false;
   m_bConnecting = false;
   m_bConnected = false;
   m_bClosing = false;
   m_bShutdown = false;
   m_bBroken = false;
   m_bPeerHealth = true;
   m_ullLingerExpiration = 0;

   // event pipe creation
#ifdef EVPIPE_OSFD
   _createOsfd(m_evPipe);
#endif // OSfd

   // security state
   m_pSecMod = 0;
   memset(m_pSecKey, 0, sizeof(m_pSecKey));
}

CUDT::CUDT(const CUDT& ancestor)
{
   m_pSndBuffer = NULL;
   m_pRcvBuffer = NULL;
   m_pSndLossList = NULL;
   m_pRcvLossList = NULL;
   m_pACKWindow = NULL;
   m_pSndTimeWindow = NULL;
   m_pRcvTimeWindow = NULL;

   m_pSndQueue = NULL;
   m_pRcvQueue = NULL;
   m_pPeerAddr = NULL;
   m_pSNode = NULL;
   m_pRNode = NULL;

   // Initilize mutex and condition variables
   initSynch();

   // Default UDT configurations
   m_iMSS = ancestor.m_iMSS;
   m_bSynSending = ancestor.m_bSynSending;
   m_bSynRecving = ancestor.m_bSynRecving;
   m_iFlightFlagSize = ancestor.m_iFlightFlagSize;
   m_iSndBufSize = ancestor.m_iSndBufSize;
   m_iRcvBufSize = ancestor.m_iRcvBufSize;
   m_Linger = ancestor.m_Linger;
   m_iUDPSndBufSize = ancestor.m_iUDPSndBufSize;
   m_iUDPRcvBufSize = ancestor.m_iUDPRcvBufSize;
   m_iSockType = ancestor.m_iSockType;
   m_iIPversion = ancestor.m_iIPversion;
   m_bRendezvous = ancestor.m_bRendezvous;
   m_iSndTimeOut = ancestor.m_iSndTimeOut;
   m_iRcvTimeOut = ancestor.m_iRcvTimeOut;
   m_bReuseAddr = true; // this must be true, because all accepted sockets shared the same port with the listener
   m_bReuseAble = true; // this must be true, because all accepted sockets shared the same port with the listener
   m_llMaxBW = ancestor.m_llMaxBW;
   m_iQos = ancestor.m_iQos;

   m_pCCFactory = ancestor.m_pCCFactory->clone();
   m_pCC = NULL;
   m_pCache = ancestor.m_pCache;

   // Initial status
   m_bOpened = false;
   m_bListening = false;
   m_bConnecting = false;
   m_bConnected = false;
   m_bClosing = false;
   m_bShutdown = false;
   m_bBroken = false;
   m_bPeerHealth = true;
   m_ullLingerExpiration = 0;

   // event pipe creation
#ifdef EVPIPE_OSFD
   _createOsfd(m_evPipe);
#endif // OSfd

   // security state
   m_pSecMod = 0;
   memset(m_pSecKey, 0, sizeof(m_pSecKey));
}

// close Osfd pair
#ifdef EVPIPE_OSFD
static void _closeOsfd(SYSSOCKET m_evPipe[])
{
	// close event pipe
	///printf("close evPipe fds:%d,%d\n", m_evPipe[0], m_evPipe[1]);
#ifndef WIN32
	close(m_evPipe[1]);
	close(m_evPipe[0]);
#else
	// notes: let user close reading Osfd
	closesocket(m_evPipe[1]);
	///closesocket(m_evPipe[0]);
#endif
}
#endif

CUDT::~CUDT()
{
   // release mutex/condtion variables
   destroySynch();

   // destroy the data structures
   delete m_pSndBuffer;
   delete m_pRcvBuffer;
   delete m_pSndLossList;
   delete m_pRcvLossList;
   delete m_pACKWindow;
   delete m_pSndTimeWindow;
   delete m_pRcvTimeWindow;
   delete m_pCCFactory;
   delete m_pCC;
   delete m_pPeerAddr;
   delete m_pSNode;
   delete m_pRNode;

   // close Osfd
#ifdef EVPIPE_OSFD
   _closeOsfd(m_evPipe);
#endif // Osfd
}

///////////////////////////////////////////////////////////////////
#ifdef EVPIPE_OSFD
// retrieve m_evPipe[0] OS fd
SYSSOCKET CUDT::getOsfd()
{
	return m_evPipe[0];
}

// feed OS fd to trigger safe edge event
// Notes: always guarantee only one byte pending
static int _feedOsfd(const SYSSOCKET m_evPipe[])
{
	char dummy;

#ifndef WIN32
// UNIX-like OS
#ifdef EVPIPE_OSFD_EDGE
	// trigger edge event
	recv(m_evPipe[0], &dummy, sizeof(dummy), 0);
	dummy = 0x68;
	return send(m_evPipe[1], &dummy, sizeof(dummy), 0);
#else
	// trigger level event
        int nread = -1;

	if ((ioctl(m_evPipe[0], FIONREAD, &nread) == 0) &&
		(nread == 0)) {
		dummy = 0x68;
		return send(m_evPipe[1], &dummy, sizeof(dummy), 0);
	} else {
		return 0;
	}
#endif

#else
// WINDOWS
	// trigger level event on windows
	unsigned long nread = -1;

	if ((ioctlsocket(m_evPipe[0], FIONREAD, &nread) == 0) &&
		(nread == 0)) {
		dummy = 0x68;
		return send(m_evPipe[1], &dummy, sizeof(dummy), 0);
	} else {
		return 0;
	}
#endif
}

int CUDT::feedOsfd()
{
	return _feedOsfd(m_evPipe);
}
#endif
//////////////////////////////////////////////////////////////////////////

void CUDT::setOpt(UDTOpt optName, const void* optval, int optlen)
{
   if (m_bBroken || m_bClosing)
      throw CUDTException(2, 1, 0);

   CGuard cg(m_ConnectionLock);

   // take serial lock
   CGuard serialguard(m_SerialLock);
   ///CGuard sendguard(m_SendLock);
   ///CGuard recvguard(m_RecvLock);

   switch (optName)
   {
   case UDT_MSS:
      if (m_bOpened)
         throw CUDTException(5, 1, 0);

      if (*(int*)optval < int(28 + CHandShake::m_iContentSize))
         throw CUDTException(5, 3, 0);

      m_iMSS = *(int*)optval;

      // Packet size cannot be greater than UDP buffer size
      if (m_iMSS > m_iUDPSndBufSize)
         m_iMSS = m_iUDPSndBufSize;
      if (m_iMSS > m_iUDPRcvBufSize)
         m_iMSS = m_iUDPRcvBufSize;

      break;

   case UDT_SNDSYN:
      m_bSynSending = *(bool *)optval;
      break;

   case UDT_RCVSYN:
      m_bSynRecving = *(bool *)optval;
      break;

   case UDT_CC:
      if (m_bConnecting || m_bConnected)
         throw CUDTException(5, 1, 0);
      if (NULL != m_pCCFactory)
         delete m_pCCFactory;
      m_pCCFactory = ((CCCVirtualFactory *)optval)->clone();

      break;

   case UDT_FC:
      if (m_bConnecting || m_bConnected)
         throw CUDTException(5, 2, 0);

      if (*(int*)optval < 1)
         throw CUDTException(5, 3);

      // Mimimum recv flight flag size is 32 packets
      if (*(int*)optval > 32)
         m_iFlightFlagSize = *(int*)optval;
      else
         m_iFlightFlagSize = 32;

      break;

   case UDT_SNDBUF:
      if (m_bOpened)
         throw CUDTException(5, 1, 0);

      if (*(int*)optval <= 0)
         throw CUDTException(5, 3, 0);

      m_iSndBufSize = *(int*)optval / (m_iMSS - 28);

      break;

   case UDT_RCVBUF:
      if (m_bOpened)
         throw CUDTException(5, 1, 0);

      if (*(int*)optval <= 0)
         throw CUDTException(5, 3, 0);

      // Mimimum recv buffer size is 32 packets
      if (*(int*)optval > (m_iMSS - 28) * 32)
         m_iRcvBufSize = *(int*)optval / (m_iMSS - 28);
      else
         m_iRcvBufSize = 32;

      // recv buffer MUST not be greater than FC size
      if (m_iRcvBufSize > m_iFlightFlagSize)
         m_iRcvBufSize = m_iFlightFlagSize;

      break;

   case UDT_LINGER:
      m_Linger = *(linger*)optval;
      break;

   case UDP_SNDBUF:
      if (m_bOpened)
         throw CUDTException(5, 1, 0);

      m_iUDPSndBufSize = *(int*)optval;

      if (m_iUDPSndBufSize < m_iMSS)
         m_iUDPSndBufSize = m_iMSS;

      break;

   case UDP_RCVBUF:
      if (m_bOpened)
         throw CUDTException(5, 1, 0);

      m_iUDPRcvBufSize = *(int*)optval;

      if (m_iUDPRcvBufSize < m_iMSS)
         m_iUDPRcvBufSize = m_iMSS;

      break;

   case UDT_RENDEZVOUS:
      if (m_bConnecting || m_bConnected)
         throw CUDTException(5, 1, 0);
      m_bRendezvous = *(bool *)optval;
      break;

   case UDT_SNDTIMEO: 
      m_iSndTimeOut = *(int*)optval; 
      break; 

   case UDT_RCVTIMEO: 
      m_iRcvTimeOut = *(int*)optval; 
      break; 

   case UDT_QOS:
      m_iQos = *(int*)optval;
      break;

   case UDT_REUSEADDR:

   #ifdef DEBUG
      ///printf("reuseaddr: %d\n", *(bool *)optval);
   #endif

      if (m_bOpened)
         throw CUDTException(5, 1, 0);
      m_bReuseAddr = *(bool*)optval;

#ifdef DEBUG
      ///printf("reuseaddr: %d done \n", *(bool *)optval);
#endif

      break;

   case UDT_MAXBW:
      m_llMaxBW = *(int64_t*)optval;
      break;

   case UDT_SECMOD:
	  m_pSecMod = *(int*)optval;
      break;

   case UDT_SECKEY:
	  memcpy(m_pSecKey, optval, (optlen < sizeof(m_pSecKey)) ? optlen : sizeof(m_pSecKey));
      break;

   case UDT_REUSEABLE:

   #ifdef DEBUG
      ///printf("reuseable: %d\n", *(bool *)optval);
   #endif

       if (m_bOpened)
           throw CUDTException(5, 1, 0);
       m_bReuseAble = *(bool *)optval;

   #ifdef DEBUG
      ///printf("reuseable: %d\n", *(bool *)optval);
   #endif

       break;

   default:
      throw CUDTException(5, 0, 0);
   }
}

void CUDT::getOpt(UDTOpt optName, void* optval, int& optlen)
{
   ///printf("%s.%s.%d\n", __FILE__, __FUNCTION__, __LINE__);
   CGuard cg(m_ConnectionLock);
   ///printf("%s.%s.%d\n", __FILE__, __FUNCTION__, __LINE__);

   switch (optName)
   {
   case UDT_MSS:
      *(int*)optval = m_iMSS;
      optlen = sizeof(int);
      break;

   case UDT_SNDSYN:
      *(bool*)optval = m_bSynSending;
      optlen = sizeof(bool);
      break;

   case UDT_RCVSYN:
      *(bool*)optval = m_bSynRecving;
      optlen = sizeof(bool);
      break;

   case UDT_CC:
      if (!m_bOpened)
         throw CUDTException(5, 5, 0);
      *(CCC**)optval = m_pCC;
      optlen = sizeof(CCC*);

      break;

   case UDT_FC:
      *(int*)optval = m_iFlightFlagSize;
      optlen = sizeof(int);
      break;

   case UDT_SNDBUF:
      *(int*)optval = m_iSndBufSize * (m_iMSS - 28);
      optlen = sizeof(int);
      break;

   case UDT_RCVBUF:
      *(int*)optval = m_iRcvBufSize * (m_iMSS - 28);
      optlen = sizeof(int);
      break;

   case UDT_LINGER:
      if (optlen < (int)(sizeof(linger)))
         throw CUDTException(5, 3, 0);

      *(linger*)optval = m_Linger;
      optlen = sizeof(linger);
      break;

   case UDP_SNDBUF:
      *(int*)optval = m_iUDPSndBufSize;
      optlen = sizeof(int);
      break;

   case UDP_RCVBUF:
      *(int*)optval = m_iUDPRcvBufSize;
      optlen = sizeof(int);
      break;

   case UDT_RENDEZVOUS:
      *(bool *)optval = m_bRendezvous;
      optlen = sizeof(bool);
      break;

   case UDT_SNDTIMEO: 
      *(int*)optval = m_iSndTimeOut; 
      optlen = sizeof(int); 
      break; 
    
   case UDT_RCVTIMEO: 
      *(int*)optval = m_iRcvTimeOut; 
      optlen = sizeof(int); 
      break; 

   case UDT_QOS:
      *(int*)optval = m_iQos;
      optlen = sizeof(int);
      break;

   case UDT_REUSEADDR:
      *(bool *)optval = m_bReuseAddr;
      optlen = sizeof(bool);
      break;

   case UDT_MAXBW:
      *(int64_t*)optval = m_llMaxBW;
      optlen = sizeof(int64_t);
      break;

   case UDT_STATE:
      *(int32_t*)optval = s_UDTUnited.getStatus(m_SocketID);
      optlen = sizeof(int32_t);
      break;

   case UDT_EVENT:
   {
      int32_t event = 0;
      if (m_bBroken)
         event |= UDT_EPOLL_ERR;
      else
      {
		 // take listen socket in
         if ((m_pRcvBuffer && (m_pRcvBuffer->getRcvDataSize() > 0)) ||
			 (m_bListening && (m_pCUDTSocket->m_pQueuedSockets->size() > 0)))
            event |= UDT_EPOLL_IN;
         if (m_pSndBuffer && (m_iSndBufSize > m_pSndBuffer->getCurrBufSize()))
            event |= UDT_EPOLL_OUT;
      }
      *(int32_t*)optval = event;
      optlen = sizeof(int32_t);
      break;
   }

   case UDT_SNDDATA:
      if (m_pSndBuffer)
         *(int32_t*)optval = m_pSndBuffer->getCurrBufSize();
      else
         *(int32_t*)optval = 0;
      optlen = sizeof(int32_t);
      break;

   case UDT_RCVDATA:
      if (m_pRcvBuffer)
         *(int32_t*)optval = m_pRcvBuffer->getRcvDataSize();
      else
         *(int32_t*)optval = 0;
      optlen = sizeof(int32_t);
      break;

#ifdef EVPIPE_OSFD
   case UDT_OSFD:
      *(SYSSOCKET*)optval = m_evPipe[0];
      optlen = sizeof(SYSSOCKET);
      break;
#endif

   case UDT_SECMOD:
	  *(int32_t*)optval = m_pSecMod;
	  optlen = sizeof(m_pSecMod);
	  break;

   case UDT_UDPFD:
      *(SYSSOCKET *)optval = s_UDTUnited.getUDPFD(m_SocketID);
      optlen = sizeof(SYSSOCKET);
      break;

   case UDT_REUSEABLE:
       *(bool *)optval = m_bReuseAble;
       optlen = sizeof(bool);
       break;

   default:
      throw CUDTException(5, 0, 0);
   }
}

void CUDT::open()
{
   ///printf("%s.%s.%d\n", __FILE__, __FUNCTION__, __LINE__);
   CGuard cg(m_ConnectionLock);
   ///printf("%s.%s.%d\n", __FILE__, __FUNCTION__, __LINE__);

   // Initial sequence number, loss, acknowledgement, etc.
   m_iPktSize = m_iMSS - 28;
   m_iPayloadSize = m_iPktSize - CPacket::m_iPktHdrSize;

   m_iEXPCount = 1;
   m_iBandwidth = 1;
   m_iDeliveryRate = 16;
   m_iAckSeqNo = 0;
   m_ullLastAckTime = 0;

   // trace information
   m_StartTime = CTimer::getTime();
   m_llSentTotal = m_llRecvTotal = m_iSndLossTotal = m_iRcvLossTotal = m_iRetransTotal = m_iSentACKTotal = m_iRecvACKTotal = m_iSentNAKTotal = m_iRecvNAKTotal = 0;
   m_LastSampleTime = CTimer::getTime();
   m_llTraceSent = m_llTraceRecv = m_iTraceSndLoss = m_iTraceRcvLoss = m_iTraceRetrans = m_iSentACK = m_iRecvACK = m_iSentNAK = m_iRecvNAK = 0;
   m_llSndDuration = m_llSndDurationTotal = 0;

   // structures for queue
   if (NULL == m_pSNode)
      m_pSNode = new CSNode;
   m_pSNode->m_pUDT = this;
   m_pSNode->m_llTimeStamp = 1;
   m_pSNode->m_iHeapLoc = -1;

   if (NULL == m_pRNode)
      m_pRNode = new CRNode;
   m_pRNode->m_pUDT = this;
   m_pRNode->m_llTimeStamp = 1;
   m_pRNode->m_pPrev = m_pRNode->m_pNext = NULL;
   m_pRNode->m_bOnList = false;

   m_iRTT = 10 * m_iSYNInterval;
   m_iRTTVar = m_iRTT >> 1;
   m_ullCPUFrequency = CTimer::getCPUFrequency();

   // set up the timers
   m_ullSYNInt = m_iSYNInterval * m_ullCPUFrequency;
  
   // set minimum NAK and EXP timeout to 100ms
   m_ullMinNakInt = 300000 * m_ullCPUFrequency;
   m_ullMinExpInt = 300000 * m_ullCPUFrequency;

   m_ullACKInt = m_ullSYNInt;
   m_ullNAKInt = m_ullMinNakInt;

   uint64_t currtime;
   CTimer::rdtsc(currtime);
   m_ullLastRspTime = currtime;
   m_ullNextACKTime = currtime + m_ullSYNInt;
   m_ullNextNAKTime = currtime + m_ullNAKInt;

   m_iPktCount = 0;
   m_iLightACKCount = 1;

   m_ullTargetTime = 0;
   m_ullTimeDiff = 0;

   // Now UDT is opened.
   m_bOpened = true;
}

void CUDT::listen()
{
   ///printf("%s.%s.%d\n", __FILE__, __FUNCTION__, __LINE__);
   CGuard cg(m_ConnectionLock);
   ///printf("%s.%s.%d\n", __FILE__, __FUNCTION__, __LINE__);

   if (!m_bOpened)
      throw CUDTException(5, 0, 0);

   if (m_bConnecting || m_bConnected)
      throw CUDTException(5, 2, 0);

   // listen can be called more than once
   if (m_bListening)
      return;

   // if there is already another socket listening on the same port
   if (m_pRcvQueue->setListener(this) < 0)
      throw CUDTException(5, 11, 0);

   m_bListening = true;
}

void CUDT::punchhole(const sockaddr* serv_addr, const int from, const int to)
{
   ///printf("%s.%s.%d\n", __FILE__, __FUNCTION__, __LINE__);
   CGuard cg(m_ConnectionLock);
   ///printf("%s.%s.%d\n", __FILE__, __FUNCTION__, __LINE__);

   if (!m_bOpened)
      throw CUDTException(5, 0, 0);

   if (m_bListening)
      throw CUDTException(5, 2, 0);

   if (m_bConnecting || m_bConnected)
      throw CUDTException(5, 2, 0);


   CPacket klpkt; //001 - Keep-alive
   klpkt.m_iID = 0;
   klpkt.pack(1);

   if (AF_INET == m_iIPversion) {
	   // create temp address
	   sockaddr_in temp_addr;
	   memcpy(&temp_addr, serv_addr, sizeof(sockaddr_in));
       
       // Send keep-alive packet to punch hole with port range [from, to]
       int refport = temp_addr.sin_port;
	   for (int start = from; start <= to; start ++) {
		   temp_addr.sin_port = refport + start;
		   m_pSndQueue->sendto((const sockaddr *)&temp_addr, klpkt);
	   }

#ifdef DEBUG
       printf("punch hole 4\n");
#endif
   } else {
	   // create temp address
	   sockaddr_in6 temp_addr;
	   memcpy(&temp_addr, serv_addr, sizeof(sockaddr_in6));
	   
	   // Send keep-alive packet to punch hole with port range [from, to]
	   int refport = temp_addr.sin6_port;
	   for (int start = from; start <= to; start ++) {
		   temp_addr.sin6_port = refport + start;
		   m_pSndQueue->sendto((const sockaddr *)&temp_addr, klpkt);
	   }

#ifdef DEBUG
       printf("punch hole 6\n");
#endif
   }
}

void CUDT::connect(const sockaddr* serv_addr)
{
   ///printf("%s.%s.%d\n", __FILE__, __FUNCTION__, __LINE__);
   CGuard cg(m_ConnectionLock);
   ///printf("%s.%s.%d\n", __FILE__, __FUNCTION__, __LINE__);

   if (!m_bOpened)
      throw CUDTException(5, 0, 0);

   if (m_bListening)
      throw CUDTException(5, 2, 0);

   if (m_bConnecting || m_bConnected)
      throw CUDTException(5, 2, 0);

   m_bConnecting = true;

   // record peer/server address
   delete m_pPeerAddr;
   m_pPeerAddr = (AF_INET == m_iIPversion) ? (sockaddr*)new sockaddr_in : (sockaddr*)new sockaddr_in6;
   memcpy(m_pPeerAddr, serv_addr, (AF_INET == m_iIPversion) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6));

   // register this socket in the rendezvous queue
   // RendezevousQueue is used to temporarily store incoming handshake, non-rendezvous connections also require this function
   uint64_t ttl = 3000000;
   if (m_bRendezvous)
      ttl *= 10;
   ttl += CTimer::getTime();
   m_pRcvQueue->registerConnector(m_SocketID, this, m_iIPversion, serv_addr, ttl);

   // This is my current configurations
   m_ConnReq.m_iVersion = m_iVersion;
   m_ConnReq.m_iType = m_iSockType;
   m_ConnReq.m_iMSS = m_iMSS;
   m_ConnReq.m_iFlightFlagSize = (m_iRcvBufSize < m_iFlightFlagSize)? m_iRcvBufSize : m_iFlightFlagSize;
   m_ConnReq.m_iReqType = (!m_bRendezvous) ? 1 : 0;
   m_ConnReq.m_iID = m_SocketID;
   CIPAddress::ntop(serv_addr, m_ConnReq.m_piPeerIP, m_iIPversion);

   // Random Initial Sequence Number
   srand((unsigned int)CTimer::getTime());
   m_iISN = m_ConnReq.m_iISN = (int32_t)(CSeqNo::m_iMaxSeqNo * (double(rand()) / RAND_MAX));

   m_iLastDecSeq = m_iISN - 1;
   m_iSndLastAck = m_iISN;
   m_iSndLastDataAck = m_iISN;
   m_iSndCurrSeqNo = m_iISN - 1;
   m_iSndLastAck2 = m_iISN;
   m_ullSndLastAck2Time = CTimer::getTime();

   // Inform the server my configurations.
   CPacket request;
   // ID = 0, connection request
   request.m_iID = 0;
   char* reqdata = new char [m_iPayloadSize];
   request.pack(0, NULL, reqdata, m_iPayloadSize);

   int hs_size = m_iPayloadSize;
   m_ConnReq.serialize(reqdata, hs_size);
   request.setLength(hs_size);
   m_pSndQueue->sendto(serv_addr, request);
   m_llLastReqTime = CTimer::getTime();

   // asynchronous connect, return immediately
   if (!m_bSynRecving)
   {
      delete [] reqdata;
      return;
   }

   // Wait for the negotiated configurations from the peer side.
   CPacket response;
   char* resdata = new char [m_iPayloadSize];
   response.pack(0, NULL, resdata, m_iPayloadSize);

   CUDTException e(0, 0);

   while (!m_bClosing)
   {
      // avoid sending too many requests, at most 1 request per 250ms
      if (CTimer::getTime() - m_llLastReqTime > 250000)
      {
         m_ConnReq.serialize(reqdata, hs_size);
         request.setLength(hs_size);
         if (m_bRendezvous)
            request.m_iID = m_ConnRes.m_iID;
         m_pSndQueue->sendto(serv_addr, request);
         m_llLastReqTime = CTimer::getTime();
      }

      response.setLength(m_iPayloadSize);
      if (m_pRcvQueue->recvfrom(m_SocketID, response) > 0)
      {
         if (connect(response) <= 0)
            break;

         // new request/response should be sent out immediately on receving a response
         m_llLastReqTime = 0;
      }

      if (CTimer::getTime() > ttl)
      {
      	 // Stop waiting for this connection
      	 m_pRcvQueue->removeConnector(m_SocketID);

         // timeout
         e = CUDTException(1, 1, 0);
         break;
      }
   }

   delete [] reqdata;
   delete [] resdata;

   if (e.getErrorCode() == 0)
   {
      if (m_bClosing)                                                 // if the socket is closed before connection...
         e = CUDTException(1);
      else if (1002 == m_ConnRes.m_iReqType)                          // connection request rejected
         e = CUDTException(1, 2, 0);
      else if ((!m_bRendezvous) && (m_iISN != m_ConnRes.m_iISN))      // secuity check
         e = CUDTException(1, 4, 0);
   }

   if (e.getErrorCode() != 0)
      throw e;
}

int CUDT::connect(const CPacket& response) throw ()
{
   // this is the 2nd half of a connection request. If the connection is setup successfully this returns 0.
   // returning -1 means there is an error.
   // returning 1 or 2 means the connection is in process and needs more handshake

   if (!m_bConnecting)
      return -1;

   if (m_bRendezvous && ((0 == response.getFlag()) || (1 == response.getType())) && (0 != m_ConnRes.m_iType))
   {
      // a data packet or a keep-alive packet comes, which means the peer side is already connected
      // in this situation, the previously recorded response will be used
      goto POST_CONNECT;
   }

   // continue on data packet arrived
   if ((1 != response.getFlag()) || (0 != response.getType()))
      return 1;

   m_ConnRes.deserialize(response.m_pcData, response.getLength());

   if (m_bRendezvous)
   {
      // regular connect should NOT communicate with rendezvous connect
      // rendezvous connect require 3-way handshake
      if (1 == m_ConnRes.m_iReqType)
         return -1;

      if ((0 == m_ConnReq.m_iReqType) || (0 == m_ConnRes.m_iReqType))
      {
         m_ConnReq.m_iReqType = -1;
         // the request time must be updated so that the next handshake can be sent out immediately.
         m_llLastReqTime = 0;
         return 1;
      }
   }
   else
   {
      // set cookie
      if (1 == m_ConnRes.m_iReqType)
      {
         m_ConnReq.m_iReqType = -1;
         m_ConnReq.m_iCookie = m_ConnRes.m_iCookie;
         m_llLastReqTime = 0;
         return 1;
      }
   }

POST_CONNECT:
   // Remove from rendezvous queue
   m_pRcvQueue->removeConnector(m_SocketID);

   // Re-configure according to the negotiated values.
   m_iMSS = m_ConnRes.m_iMSS;
   m_iFlowWindowSize = m_ConnRes.m_iFlightFlagSize;
   m_iPktSize = m_iMSS - 28;
   m_iPayloadSize = m_iPktSize - CPacket::m_iPktHdrSize;
   m_iPeerISN = m_ConnRes.m_iISN;
   m_iRcvLastAck = m_ConnRes.m_iISN;
   m_iRcvLastAckAck = m_ConnRes.m_iISN;
   m_iRcvCurrSeqNo = m_ConnRes.m_iISN - 1;
   m_PeerID = m_ConnRes.m_iID;
   memcpy(m_piSelfIP, m_ConnRes.m_piPeerIP, 16);

   // Record cookie
   m_pCookie = m_ConnRes.m_iCookie;

   // Prepare all data structures
   try
   {
      m_pSndBuffer = new CSndBuffer(32, m_iPayloadSize);
      m_pRcvBuffer = new CRcvBuffer(&(m_pRcvQueue->m_UnitQueue), m_iRcvBufSize);
      // after introducing lite ACK, the sndlosslist may not be cleared in time, so it requires twice space.
      m_pSndLossList = new CSndLossList(m_iFlowWindowSize * 2);
      m_pRcvLossList = new CRcvLossList(m_iFlightFlagSize);
      m_pACKWindow = new CACKWindow(1024);
      m_pRcvTimeWindow = new CPktTimeWindow(16, 64);
      m_pSndTimeWindow = new CPktTimeWindow();
   }
   catch (...)
   {
      throw CUDTException(3, 2, 0);
   }

   CInfoBlock ib;
   ib.m_iIPversion = m_iIPversion;
   CInfoBlock::convert(m_pPeerAddr, m_iIPversion, ib.m_piIP);
   if (m_pCache->lookup(&ib) >= 0)
   {
      m_iRTT = ib.m_iRTT;
      m_iBandwidth = ib.m_iBandwidth;
   }

   m_pCC = m_pCCFactory->create();
   m_pCC->m_UDT = m_SocketID;
   m_pCC->setMSS(m_iMSS);
   m_pCC->setMaxCWndSize(m_iFlowWindowSize);
   m_pCC->setSndCurrSeqNo(m_iSndCurrSeqNo);
   m_pCC->setRcvRate(m_iDeliveryRate);
   m_pCC->setRTT(m_iRTT);
   m_pCC->setBandwidth(m_iBandwidth);
   m_pCC->init();

   m_ullInterval = (uint64_t)(m_pCC->m_dPktSndPeriod * m_ullCPUFrequency);
   m_dCongestionWindow = m_pCC->m_dCWndSize;

   // And, I am connected too.
   m_bConnecting = false;
   m_bConnected = true;

   // register this socket for receiving data packets
   m_pRNode->m_bOnList = true;
   m_pRcvQueue->setNewEntry(this);

   // acknowledge the management module.
   s_UDTUnited.connect_complete(m_SocketID);

   // acknowledge any waiting epolls to write
   s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, true);

#ifdef EVPIPE_OSFD
   // trigger event pipe
   ///printf("%s.%s.%d, trigger Connected...", __FILE__, __FUNCTION__, __LINE__);
   feedOsfd();
   ///printf("done\n");
#endif

   return 0;
}

void CUDT::connect(const sockaddr* peer, CHandShake* hs)
{
   ///printf("%s.%s.%d\n", __FILE__, __FUNCTION__, __LINE__);
   CGuard cg(m_ConnectionLock);
   ///printf("%s.%s.%d\n", __FILE__, __FUNCTION__, __LINE__);

   // Uses the smaller MSS between the peers        
   if (hs->m_iMSS > m_iMSS)
      hs->m_iMSS = m_iMSS;
   else
      m_iMSS = hs->m_iMSS;

   // exchange info for maximum flow window size
   m_iFlowWindowSize = hs->m_iFlightFlagSize;
   hs->m_iFlightFlagSize = (m_iRcvBufSize < m_iFlightFlagSize)? m_iRcvBufSize : m_iFlightFlagSize;

   m_iPeerISN = hs->m_iISN;

   m_iRcvLastAck = hs->m_iISN;
   m_iRcvLastAckAck = hs->m_iISN;
   m_iRcvCurrSeqNo = hs->m_iISN - 1;

   m_PeerID = hs->m_iID;
   hs->m_iID = m_SocketID;

   // Record cookie
   m_pCookie = hs->m_iCookie;

   // use peer's ISN and send it back for security check
   m_iISN = hs->m_iISN;

   m_iLastDecSeq = m_iISN - 1;
   m_iSndLastAck = m_iISN;
   m_iSndLastDataAck = m_iISN;
   m_iSndCurrSeqNo = m_iISN - 1;
   m_iSndLastAck2 = m_iISN;
   m_ullSndLastAck2Time = CTimer::getTime();

   // this is a reponse handshake
   hs->m_iReqType = -1;

   // get local IP address and send the peer its IP address (because UDP cannot get local IP address)
   memcpy(m_piSelfIP, hs->m_piPeerIP, 16);
   CIPAddress::ntop(peer, hs->m_piPeerIP, m_iIPversion);
  
   m_iPktSize = m_iMSS - 28;
   m_iPayloadSize = m_iPktSize - CPacket::m_iPktHdrSize;

   // Prepare all structures
   try
   {
      m_pSndBuffer = new CSndBuffer(32, m_iPayloadSize);
      m_pRcvBuffer = new CRcvBuffer(&(m_pRcvQueue->m_UnitQueue), m_iRcvBufSize);
      m_pSndLossList = new CSndLossList(m_iFlowWindowSize * 2);
      m_pRcvLossList = new CRcvLossList(m_iFlightFlagSize);
      m_pACKWindow = new CACKWindow(1024);
      m_pRcvTimeWindow = new CPktTimeWindow(16, 64);
      m_pSndTimeWindow = new CPktTimeWindow();
   }
   catch (...)
   {
      throw CUDTException(3, 2, 0);
   }

   CInfoBlock ib;
   ib.m_iIPversion = m_iIPversion;
   CInfoBlock::convert(peer, m_iIPversion, ib.m_piIP);
   if (m_pCache->lookup(&ib) >= 0)
   {
      m_iRTT = ib.m_iRTT;
      m_iBandwidth = ib.m_iBandwidth;
   }

   m_pCC = m_pCCFactory->create();
   m_pCC->m_UDT = m_SocketID;
   m_pCC->setMSS(m_iMSS);
   m_pCC->setMaxCWndSize(m_iFlowWindowSize);
   m_pCC->setSndCurrSeqNo(m_iSndCurrSeqNo);
   m_pCC->setRcvRate(m_iDeliveryRate);
   m_pCC->setRTT(m_iRTT);
   m_pCC->setBandwidth(m_iBandwidth);
   m_pCC->init();

   m_ullInterval = (uint64_t)(m_pCC->m_dPktSndPeriod * m_ullCPUFrequency);
   m_dCongestionWindow = m_pCC->m_dCWndSize;

   delete m_pPeerAddr;
   m_pPeerAddr = (AF_INET == m_iIPversion) ? (sockaddr*)new sockaddr_in : (sockaddr*)new sockaddr_in6;
   memcpy(m_pPeerAddr, peer, (AF_INET == m_iIPversion) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6));

   // And of course, it is connected.
   m_bConnected = true;

   // register this socket for receiving data packets
   m_pRNode->m_bOnList = true;
   m_pRcvQueue->setNewEntry(this);

   //send the response to the peer, see listen() for more discussions about this
   int size = CHandShake::m_iContentSize;
   char* buffer = new char[size];
   hs->serialize(buffer, size);
   CPacket response;
   response.m_iID = m_PeerID;
   response.pack(0, NULL, buffer, size);
   m_pSndQueue->sendto(peer, response);
   delete [] buffer;
}

void CUDT::close()
{
   if (!m_bOpened)
      return;

   if (0 != m_Linger.l_onoff)
   {
      uint64_t entertime = CTimer::getTime();

      while (!m_bBroken && m_bConnected && (m_pSndBuffer->getCurrBufSize() > 0) && (CTimer::getTime() - entertime < m_Linger.l_linger * 1000000ULL))
      {
         // linger has been checked by previous close() call and has expired
         if (m_ullLingerExpiration >= entertime)
            break;

         if (!m_bSynSending)
         {
            // if this socket enables asynchronous sending, return immediately and let GC to close it later
            if (0 == m_ullLingerExpiration)
               m_ullLingerExpiration = entertime + m_Linger.l_linger * 1000000ULL;

            return;
         }

         #ifndef WIN32
            timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 1000000;
            nanosleep(&ts, NULL);
         #else
            Sleep(1);
         #endif
      }
   }

   // remove this socket from the snd queue
   if (m_bConnected)
      m_pSndQueue->m_pSndUList->remove(this);

#ifdef EVPIPE_OSFD
   // trigger event pipe to notify closing
   ///printf("%s.%s.%d, trigger Closing...", __FILE__, __FUNCTION__, __LINE__);
   ///feedOsfd();
   ///printf("done\n");
#endif

   // trigger any pending IO events.
   s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_ERR, true);
   // then remove itself from all epoll monitoring
   try
   {
      for (set<int>::iterator i = m_sPollID.begin(); i != m_sPollID.end(); ++ i)
         s_UDTUnited.m_EPoll.remove_usock(*i, m_SocketID);
   }
   catch (...)
   {
   }

   if (!m_bOpened)
      return;

   // Inform the threads handler to stop.
   m_bClosing = true;

   ///printf("%s.%s.%d\n", __FILE__, __FUNCTION__, __LINE__);
   CGuard cg(m_ConnectionLock);
   ///printf("%s.%s.%d\n", __FILE__, __FUNCTION__, __LINE__);

#ifndef EVPIPE_OSFD
   // Signal the sender and recver if they are waiting for data.
   ///printf("%s.%s.%d\n", __FILE__, __FUNCTION__, __LINE__);
   releaseSynch();
   ///printf("%s.%s.%d\n", __FILE__, __FUNCTION__, __LINE__);
#endif

   if (m_bListening)
   {
      m_bListening = false;
      m_pRcvQueue->removeListener(this);
   }
   else if (m_bConnecting)
   {
      m_pRcvQueue->removeConnector(m_SocketID);
   }

   if (m_bConnected)
   {
      if (!m_bShutdown)
         sendCtrl(5);

      m_pCC->close();

      // Store current connection information.
      CInfoBlock ib;
      ib.m_iIPversion = m_iIPversion;
      CInfoBlock::convert(m_pPeerAddr, m_iIPversion, ib.m_piIP);
      ib.m_iRTT = m_iRTT;
      ib.m_iBandwidth = m_iBandwidth;
      m_pCache->update(&ib);

      m_bConnected = false;
   }

   // take serial lock
   // notes: bypass it to avoid inter-lock. to be root cause it
   ///printf("%s.%s.%d\n", __FILE__, __FUNCTION__, __LINE__);
   ///CGuard serialguard(m_SerialLock);
   ///printf("%s.%s.%d\n", __FILE__, __FUNCTION__, __LINE__);

#ifndef EVPIPE_OSFD
   // waiting all send and recv calls to stop
   ///printf("%s.%s.%d\n", __FILE__, __FUNCTION__, __LINE__);
   //CGuard sendguard(m_SendLock);
   ///printf("%s.%s.%d\n", __FILE__, __FUNCTION__, __LINE__);
   //CGuard recvguard(m_RecvLock);
   ///printf("%s.%s.%d\n", __FILE__, __FUNCTION__, __LINE__);
#endif

   // CLOSED.
   m_bOpened = false;
}

int CUDT::send(const char* data, int len)
{
   if (UDT_DGRAM == m_iSockType)
      throw CUDTException(5, 10, 0);

   // throw an exception if not connected
   if (m_bBroken || m_bClosing)
      throw CUDTException(2, 1, 0);
   else if (!m_bConnected)
      throw CUDTException(2, 2, 0);

   if (len <= 0)
      return 0;

   ///printf("%s.%s.%d\n", __FILE__, __FUNCTION__, __LINE__);
   // take serial lock
   CGuard serialguard(m_SerialLock);
   ///CGuard sendguard(m_SendLock);
   ///printf("%s.%s.%d\n", __FILE__, __FUNCTION__, __LINE__);

   if (m_pSndBuffer->getCurrBufSize() == 0)
   {
      // delay the EXP timer to avoid mis-fired timeout
      uint64_t currtime;
      CTimer::rdtsc(currtime);
      m_ullLastRspTime = currtime;
   }

   if (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize())
   {
      if (!m_bSynSending)
         throw CUDTException(6, 1, 0);
      else
      {
         // wait here during a blocking sending
         #ifndef WIN32
            pthread_mutex_lock(&m_SendBlockLock);
            if (m_iSndTimeOut < 0) 
            { 
               while (!m_bBroken && m_bConnected && !m_bClosing && (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize()) && m_bPeerHealth)
                  pthread_cond_wait(&m_SendBlockCond, &m_SendBlockLock);
            }
            else
            {
               uint64_t exptime = CTimer::getTime() + m_iSndTimeOut * 1000ULL;
               timespec locktime; 
    
               locktime.tv_sec = exptime / 1000000;
               locktime.tv_nsec = (exptime % 1000000) * 1000;

               while (!m_bBroken && m_bConnected && !m_bClosing && (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize()) && m_bPeerHealth && (CTimer::getTime() < exptime))
                  pthread_cond_timedwait(&m_SendBlockCond, &m_SendBlockLock, &locktime);
            }
            pthread_mutex_unlock(&m_SendBlockLock);
         #else
            if (m_iSndTimeOut < 0)
            {
               while (!m_bBroken && m_bConnected && !m_bClosing && (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize()) && m_bPeerHealth)
                  WaitForSingleObject(m_SendBlockCond, INFINITE);
            }
            else 
            {
               uint64_t exptime = CTimer::getTime() + m_iSndTimeOut * 1000ULL;

               while (!m_bBroken && m_bConnected && !m_bClosing && (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize()) && m_bPeerHealth && (CTimer::getTime() < exptime))
                  WaitForSingleObject(m_SendBlockCond, DWORD((exptime - CTimer::getTime()) / 1000)); 
            }
         #endif

         // check the connection status
         if (m_bBroken || m_bClosing)
            throw CUDTException(2, 1, 0);
         else if (!m_bConnected)
            throw CUDTException(2, 2, 0);
         else if (!m_bPeerHealth)
         {
            m_bPeerHealth = true;
            throw CUDTException(7);
         }
      }
   }

   if (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize())
   {
      if (m_iSndTimeOut >= 0)
         throw CUDTException(6, 3, 0); 

      return 0;
   }

   int size = (m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iPayloadSize;
   if (size > len)
      size = len;

   // record total time used for sending
   if (0 == m_pSndBuffer->getCurrBufSize())
      m_llSndDurationCounter = CTimer::getTime();

   // insert the user buffer into the sening list
   m_pSndBuffer->addBuffer(data, size);

   // insert this socket to snd list if it is not on the list yet
   // notes: if it's high priority socket, then reschedule it immediately
   if (m_iQos)
	   m_pSndQueue->m_pSndUList->update(this);
   else
	   m_pSndQueue->m_pSndUList->update(this, false);

   if (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize())
   {
      // write is not available any more
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, false);
   }

   return size;
}

int CUDT::recv(char* data, int len)
{
   if (UDT_DGRAM == m_iSockType)
      throw CUDTException(5, 10, 0);

   // throw an exception if not connected
   if (!m_bConnected)
      throw CUDTException(2, 2, 0);
   else if ((m_bBroken || m_bClosing) && (0 == m_pRcvBuffer->getRcvDataSize()))
      throw CUDTException(2, 1, 0);

   if (len <= 0)
      return 0;

   ///printf("%s.%s.%d\n", __FILE__, __FUNCTION__, __LINE__);
   // take serial lock
   CGuard serialguard(m_SerialLock);
   ///printf("%s.%s.%d\n", __FILE__, __FUNCTION__, __LINE__);

   ///CGuard recvguard(m_RecvLock);

   if (0 == m_pRcvBuffer->getRcvDataSize())
   {
      if (!m_bSynRecving)
         throw CUDTException(6, 2, 0);
      else
      {
         #ifndef WIN32
            pthread_mutex_lock(&m_RecvDataLock);
            if (m_iRcvTimeOut < 0) 
            { 
               while (!m_bBroken && m_bConnected && !m_bClosing && (0 == m_pRcvBuffer->getRcvDataSize()))
                  pthread_cond_wait(&m_RecvDataCond, &m_RecvDataLock);
            }
            else
            {
               uint64_t exptime = CTimer::getTime() + m_iRcvTimeOut * 1000ULL; 
               timespec locktime; 
    
               locktime.tv_sec = exptime / 1000000;
               locktime.tv_nsec = (exptime % 1000000) * 1000;

               while (!m_bBroken && m_bConnected && !m_bClosing && (0 == m_pRcvBuffer->getRcvDataSize()))
               {
                  pthread_cond_timedwait(&m_RecvDataCond, &m_RecvDataLock, &locktime); 
                  if (CTimer::getTime() >= exptime)
                     break;
               }
            }
            pthread_mutex_unlock(&m_RecvDataLock);
         #else
            if (m_iRcvTimeOut < 0)
            {
               while (!m_bBroken && m_bConnected && !m_bClosing && (0 == m_pRcvBuffer->getRcvDataSize()))
                  WaitForSingleObject(m_RecvDataCond, INFINITE);
            }
            else
            {
               uint64_t enter_time = CTimer::getTime();

               while (!m_bBroken && m_bConnected && !m_bClosing && (0 == m_pRcvBuffer->getRcvDataSize()))
               {
                  int diff = int(CTimer::getTime() - enter_time) / 1000;
                  if (diff >= m_iRcvTimeOut)
                      break;
                  WaitForSingleObject(m_RecvDataCond, DWORD(m_iRcvTimeOut - diff ));
               }
            }
         #endif
      }
   }

   // throw an exception if not connected
   if (!m_bConnected)
      throw CUDTException(2, 2, 0);
   else if ((m_bBroken || m_bClosing) && (0 == m_pRcvBuffer->getRcvDataSize()))
      throw CUDTException(2, 1, 0);

   int res = m_pRcvBuffer->readBuffer(data, len);

   if (m_pRcvBuffer->getRcvDataSize() <= 0)
   {
      // read is not available any more
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, false);
   }

   if ((res <= 0) && (m_iRcvTimeOut >= 0))
      throw CUDTException(6, 3, 0);

   return res;
}

int CUDT::sendmsg(const char* data, int len, int msttl, bool inorder)
{
   if (UDT_STREAM == m_iSockType)
      throw CUDTException(5, 9, 0);

   // throw an exception if not connected
   if (m_bBroken || m_bClosing)
      throw CUDTException(2, 1, 0);
   else if (!m_bConnected)
      throw CUDTException(2, 2, 0);

   if (len <= 0)
      return 0;

   if (len > m_iSndBufSize * m_iPayloadSize)
      throw CUDTException(5, 12, 0);

   // take serial lock
   CGuard serialguard(m_SerialLock);
   ///CGuard sendguard(m_SendLock);

   if (m_pSndBuffer->getCurrBufSize() == 0)
   {
      // delay the EXP timer to avoid mis-fired timeout
      uint64_t currtime;
      CTimer::rdtsc(currtime);
      m_ullLastRspTime = currtime;
   }

   if ((m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iPayloadSize < len)
   {
      if (!m_bSynSending)
         throw CUDTException(6, 1, 0);
      else
      {
         // wait here during a blocking sending
         #ifndef WIN32
            pthread_mutex_lock(&m_SendBlockLock);
            if (m_iSndTimeOut < 0)
            {
               while (!m_bBroken && m_bConnected && !m_bClosing && ((m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iPayloadSize < len))
                  pthread_cond_wait(&m_SendBlockCond, &m_SendBlockLock);
            }
            else
            {
               uint64_t exptime = CTimer::getTime() + m_iSndTimeOut * 1000ULL;
               timespec locktime;

               locktime.tv_sec = exptime / 1000000;
               locktime.tv_nsec = (exptime % 1000000) * 1000;

               while (!m_bBroken && m_bConnected && !m_bClosing && ((m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iPayloadSize < len) && (CTimer::getTime() < exptime))
                  pthread_cond_timedwait(&m_SendBlockCond, &m_SendBlockLock, &locktime);
            }
            pthread_mutex_unlock(&m_SendBlockLock);
         #else
            if (m_iSndTimeOut < 0)
            {
               while (!m_bBroken && m_bConnected && !m_bClosing && ((m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iPayloadSize < len))
                  WaitForSingleObject(m_SendBlockCond, INFINITE);
            }
            else
            {
               uint64_t exptime = CTimer::getTime() + m_iSndTimeOut * 1000ULL;

               while (!m_bBroken && m_bConnected && !m_bClosing && ((m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iPayloadSize < len) && (CTimer::getTime() < exptime))
                  WaitForSingleObject(m_SendBlockCond, DWORD((exptime - CTimer::getTime()) / 1000));
            }
         #endif

         // check the connection status
         if (m_bBroken || m_bClosing)
            throw CUDTException(2, 1, 0);
         else if (!m_bConnected)
            throw CUDTException(2, 2, 0);
      }
   }

   if ((m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iPayloadSize < len)
   {
      if (m_iSndTimeOut >= 0)
         throw CUDTException(6, 3, 0);

      return 0;
   }

   // record total time used for sending
   if (0 == m_pSndBuffer->getCurrBufSize())
      m_llSndDurationCounter = CTimer::getTime();

   // insert the user buffer into the sening list
   m_pSndBuffer->addBuffer(data, len, msttl, inorder);

   // insert this socket to the snd list if it is not on the list yet
   // notes: if it's high priority socket, then reschedule it immediately
   if (m_iQos)
	   m_pSndQueue->m_pSndUList->update(this);
   else
	   m_pSndQueue->m_pSndUList->update(this, false);

   if (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize())
   {
      // write is not available any more
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, false);
   }

   return len;   
}

int CUDT::recvmsg(char* data, int len)
{
   if (UDT_STREAM == m_iSockType)
      throw CUDTException(5, 9, 0);

   // throw an exception if not connected
   if (!m_bConnected)
      throw CUDTException(2, 2, 0);

   if (len <= 0)
      return 0;

   // take serial lock
   CGuard serialguard(m_SerialLock);
   ///CGuard recvguard(m_RecvLock);

   if (m_bBroken || m_bClosing)
   {
      int res = m_pRcvBuffer->readMsg(data, len);

      if (m_pRcvBuffer->getRcvMsgNum() <= 0)
      {
         // read is not available any more
         s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, false);
      }

      if (0 == res)
         throw CUDTException(2, 1, 0);
      else
         return res;
   }

   if (!m_bSynRecving)
   {
      int res = m_pRcvBuffer->readMsg(data, len);
      if (0 == res)
         throw CUDTException(6, 2, 0);
      else
         return res;
   }

   int res = 0;
   bool timeout = false;

   do
   {
      #ifndef WIN32
         pthread_mutex_lock(&m_RecvDataLock);

         if (m_iRcvTimeOut < 0)
         {
            while (!m_bBroken && m_bConnected && !m_bClosing && (0 == (res = m_pRcvBuffer->readMsg(data, len))))
               pthread_cond_wait(&m_RecvDataCond, &m_RecvDataLock);
         }
         else
         {
            uint64_t exptime = CTimer::getTime() + m_iRcvTimeOut * 1000ULL;
            timespec locktime;

            locktime.tv_sec = exptime / 1000000;
            locktime.tv_nsec = (exptime % 1000000) * 1000;

            if (pthread_cond_timedwait(&m_RecvDataCond, &m_RecvDataLock, &locktime) == ETIMEDOUT)
               timeout = true;

            res = m_pRcvBuffer->readMsg(data, len);           
         }
         pthread_mutex_unlock(&m_RecvDataLock);
      #else
         if (m_iRcvTimeOut < 0)
         {
            while (!m_bBroken && m_bConnected && !m_bClosing && (0 == (res = m_pRcvBuffer->readMsg(data, len))))
               WaitForSingleObject(m_RecvDataCond, INFINITE);
         }
         else
         {
            if (WaitForSingleObject(m_RecvDataCond, DWORD(m_iRcvTimeOut)) == WAIT_TIMEOUT)
               timeout = true;

            res = m_pRcvBuffer->readMsg(data, len);
         }
      #endif

      if (m_bBroken || m_bClosing)
         throw CUDTException(2, 1, 0);
      else if (!m_bConnected)
         throw CUDTException(2, 2, 0);
   } while ((0 == res) && !timeout);

   if (m_pRcvBuffer->getRcvMsgNum() <= 0)
   {
      // read is not available any more
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, false);
   }

   if ((res <= 0) && (m_iRcvTimeOut >= 0))
      throw CUDTException(6, 3, 0);

   return res;
}

int64_t CUDT::sendfile(fstream& ifs, int64_t& offset, int64_t size, int block)
{
   if (UDT_DGRAM == m_iSockType)
      throw CUDTException(5, 10, 0);

   if (m_bBroken || m_bClosing)
      throw CUDTException(2, 1, 0);
   else if (!m_bConnected)
      throw CUDTException(2, 2, 0);

   if (size <= 0)
      return 0;

   // take serial lock
   CGuard serialguard(m_SerialLock);
   ///CGuard sendguard(m_SendLock);

   if (m_pSndBuffer->getCurrBufSize() == 0)
   {
      // delay the EXP timer to avoid mis-fired timeout
      uint64_t currtime;
      CTimer::rdtsc(currtime);
      m_ullLastRspTime = currtime;
   }

   int64_t tosend = size;
   int unitsize;

   // positioning...
   try
   {
      ifs.seekg((streamoff)offset);
   }
   catch (...)
   {
      throw CUDTException(4, 1);
   }

   // sending block by block
   while (tosend > 0)
   {
      if (ifs.fail())
         throw CUDTException(4, 4);

      if (ifs.eof())
         break;

      unitsize = int((tosend >= block) ? block : tosend);

      #ifndef WIN32
         pthread_mutex_lock(&m_SendBlockLock);
         while (!m_bBroken && m_bConnected && !m_bClosing && (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize()) && m_bPeerHealth)
            pthread_cond_wait(&m_SendBlockCond, &m_SendBlockLock);
         pthread_mutex_unlock(&m_SendBlockLock);
      #else
         while (!m_bBroken && m_bConnected && !m_bClosing && (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize()) && m_bPeerHealth)
            WaitForSingleObject(m_SendBlockCond, INFINITE);
      #endif

      if (m_bBroken || m_bClosing)
         throw CUDTException(2, 1, 0);
      else if (!m_bConnected)
         throw CUDTException(2, 2, 0);
      else if (!m_bPeerHealth)
      {
         // reset peer health status, once this error returns, the app should handle the situation at the peer side
         m_bPeerHealth = true;
         throw CUDTException(7);
      }

      // record total time used for sending
      if (0 == m_pSndBuffer->getCurrBufSize())
         m_llSndDurationCounter = CTimer::getTime();

      int64_t sentsize = m_pSndBuffer->addBufferFromFile(ifs, unitsize);

      if (sentsize > 0)
      {
         tosend -= sentsize;
         offset += sentsize;
      }

      // insert this socket to snd list if it is not on the list yet
      // notes: if it's high priority socket, then reschedule it immediately
      if (m_iQos)
   	   m_pSndQueue->m_pSndUList->update(this);
      else
   	   m_pSndQueue->m_pSndUList->update(this, false);
   }

   if (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize())
   {
      // write is not available any more
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, false);
   }

   return size - tosend;
}

int64_t CUDT::recvfile(fstream& ofs, int64_t& offset, int64_t size, int block)
{
   if (UDT_DGRAM == m_iSockType)
      throw CUDTException(5, 10, 0);

   if (!m_bConnected)
      throw CUDTException(2, 2, 0);
   else if ((m_bBroken || m_bClosing) && (0 == m_pRcvBuffer->getRcvDataSize()))
      throw CUDTException(2, 1, 0);

   if (size <= 0)
      return 0;

   // take serial lock
   CGuard serialguard(m_SerialLock);
   ///CGuard recvguard(m_RecvLock);

   int64_t torecv = size;
   int unitsize = block;
   int recvsize;

   // positioning...
   try
   {
      ofs.seekp((streamoff)offset);
   }
   catch (...)
   {
      throw CUDTException(4, 3);
   }

   // receiving... "recvfile" is always blocking
   while (torecv > 0)
   {
      if (ofs.fail())
      {
         // send the sender a signal so it will not be blocked forever
         int32_t err_code = CUDTException::EFILE;
         sendCtrl(8, &err_code);

         throw CUDTException(4, 4);
      }

      #ifndef WIN32
         pthread_mutex_lock(&m_RecvDataLock);
         while (!m_bBroken && m_bConnected && !m_bClosing && (0 == m_pRcvBuffer->getRcvDataSize()))
            pthread_cond_wait(&m_RecvDataCond, &m_RecvDataLock);
         pthread_mutex_unlock(&m_RecvDataLock);
      #else
         while (!m_bBroken && m_bConnected && !m_bClosing && (0 == m_pRcvBuffer->getRcvDataSize()))
            WaitForSingleObject(m_RecvDataCond, INFINITE);
      #endif

      if (!m_bConnected)
         throw CUDTException(2, 2, 0);
      else if ((m_bBroken || m_bClosing) && (0 == m_pRcvBuffer->getRcvDataSize()))
         throw CUDTException(2, 1, 0);

      unitsize = int((torecv >= block) ? block : torecv);
      recvsize = m_pRcvBuffer->readBufferToFile(ofs, unitsize);

      if (recvsize > 0)
      {
         torecv -= recvsize;
         offset += recvsize;
      }
   }

   if (m_pRcvBuffer->getRcvDataSize() <= 0)
   {
      // read is not available any more
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, false);
   }

   return size - torecv;
}

void CUDT::sample(CPerfMon* perf, bool clear)
{
   if (!m_bConnected)
      throw CUDTException(2, 2, 0);
   if (m_bBroken || m_bClosing)
      throw CUDTException(2, 1, 0);

   uint64_t currtime = CTimer::getTime();
   perf->msTimeStamp = (currtime - m_StartTime) / 1000;

   perf->pktSent = m_llTraceSent;
   perf->pktRecv = m_llTraceRecv;
   perf->pktSndLoss = m_iTraceSndLoss;
   perf->pktRcvLoss = m_iTraceRcvLoss;
   perf->pktRetrans = m_iTraceRetrans;
   perf->pktSentACK = m_iSentACK;
   perf->pktRecvACK = m_iRecvACK;
   perf->pktSentNAK = m_iSentNAK;
   perf->pktRecvNAK = m_iRecvNAK;
   perf->usSndDuration = m_llSndDuration;

   perf->pktSentTotal = m_llSentTotal;
   perf->pktRecvTotal = m_llRecvTotal;
   perf->pktSndLossTotal = m_iSndLossTotal;
   perf->pktRcvLossTotal = m_iRcvLossTotal;
   perf->pktRetransTotal = m_iRetransTotal;
   perf->pktSentACKTotal = m_iSentACKTotal;
   perf->pktRecvACKTotal = m_iRecvACKTotal;
   perf->pktSentNAKTotal = m_iSentNAKTotal;
   perf->pktRecvNAKTotal = m_iRecvNAKTotal;
   perf->usSndDurationTotal = m_llSndDurationTotal;

   double interval = double(currtime - m_LastSampleTime);

   perf->mbpsSendRate = double(m_llTraceSent) * m_iPayloadSize * 8.0 / interval;
   perf->mbpsRecvRate = double(m_llTraceRecv) * m_iPayloadSize * 8.0 / interval;

   perf->usPktSndPeriod = m_ullInterval / double(m_ullCPUFrequency);
   perf->pktFlowWindow = m_iFlowWindowSize;
   perf->pktCongestionWindow = (int)m_dCongestionWindow;
   perf->pktFlightSize = CSeqNo::seqlen(m_iSndLastAck, CSeqNo::incseq(m_iSndCurrSeqNo)) - 1;
   perf->msRTT = m_iRTT/1000.0;
   perf->mbpsBandwidth = m_iBandwidth * m_iPayloadSize * 8.0 / 1000000.0;

   #ifndef WIN32
      if (0 == pthread_mutex_trylock(&m_ConnectionLock))
   #else
      if (WAIT_OBJECT_0 == WaitForSingleObject(m_ConnectionLock, 0))
   #endif
   {
      perf->byteAvailSndBuf = (NULL == m_pSndBuffer) ? 0 : (m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iMSS;
      perf->byteAvailRcvBuf = (NULL == m_pRcvBuffer) ? 0 : m_pRcvBuffer->getAvailBufSize() * m_iMSS;

      #ifndef WIN32
         pthread_mutex_unlock(&m_ConnectionLock);
      #else
         ReleaseMutex(m_ConnectionLock);
      #endif
   }
   else
   {
      perf->byteAvailSndBuf = 0;
      perf->byteAvailRcvBuf = 0;
   }

   if (clear)
   {
      m_llTraceSent = m_llTraceRecv = m_iTraceSndLoss = m_iTraceRcvLoss = m_iTraceRetrans = m_iSentACK = m_iRecvACK = m_iSentNAK = m_iRecvNAK = 0;
      m_llSndDuration = 0;
      m_LastSampleTime = currtime;
   }
}

void CUDT::CCUpdate()
{
   m_ullInterval = (uint64_t)(m_pCC->m_dPktSndPeriod * m_ullCPUFrequency);
   m_dCongestionWindow = m_pCC->m_dCWndSize;

   if (m_llMaxBW <= 0)
      return;
   const double minSP = 1000000.0 / (double(m_llMaxBW) / m_iMSS) * m_ullCPUFrequency;
   if (m_ullInterval < minSP)
       m_ullInterval = minSP;
}

void CUDT::initSynch()
{
   #ifndef WIN32
	  pthread_mutex_init(&m_SerialLock, NULL);

      pthread_mutex_init(&m_SendBlockLock, NULL);
      pthread_cond_init(&m_SendBlockCond, NULL);
      pthread_mutex_init(&m_RecvDataLock, NULL);
      pthread_cond_init(&m_RecvDataCond, NULL);
      ///pthread_mutex_init(&m_SendLock, NULL);
      ///pthread_mutex_init(&m_RecvLock, NULL);
      pthread_mutex_init(&m_AckLock, NULL);
      pthread_mutex_init(&m_ConnectionLock, NULL);
   #else
      m_SerialLock = CreateMutex(NULL, false, NULL);

      m_SendBlockLock = CreateMutex(NULL, false, NULL);
      m_SendBlockCond = CreateEvent(NULL, false, false, NULL);
      m_RecvDataLock = CreateMutex(NULL, false, NULL);
      m_RecvDataCond = CreateEvent(NULL, false, false, NULL);
      ///m_SendLock = CreateMutex(NULL, false, NULL);
      ///m_RecvLock = CreateMutex(NULL, false, NULL);
      m_AckLock = CreateMutex(NULL, false, NULL);
      m_ConnectionLock = CreateMutex(NULL, false, NULL);
   #endif

      ///////////////////////////////////
      // sanity checking on mutex
      CGuard::enterCS(m_SerialLock);
      CGuard::leaveCS(m_SerialLock);

      CGuard::enterCS(m_SendBlockLock);
      CGuard::leaveCS(m_SendBlockLock);

      CGuard::enterCS(m_RecvDataLock);
      CGuard::leaveCS(m_RecvDataLock);

      ///CGuard::enterCS(m_SendLock);
      ///CGuard::leaveCS(m_SendLock);

      ///CGuard::enterCS(m_RecvLock);
      ///CGuard::leaveCS(m_RecvLock);

      CGuard::enterCS(m_AckLock);
      CGuard::leaveCS(m_AckLock);

      CGuard::enterCS(m_ConnectionLock);
      CGuard::leaveCS(m_ConnectionLock);
      ///////////////////////////////////
}

void CUDT::destroySynch()
{
   #ifndef WIN32
      pthread_mutex_destroy(&m_SendBlockLock);
      pthread_cond_destroy(&m_SendBlockCond);
      pthread_mutex_destroy(&m_RecvDataLock);
      pthread_cond_destroy(&m_RecvDataCond);
      ///pthread_mutex_destroy(&m_SendLock);
      ///pthread_mutex_destroy(&m_RecvLock);
      pthread_mutex_destroy(&m_AckLock);
      pthread_mutex_destroy(&m_ConnectionLock);

      pthread_mutex_destroy(&m_SerialLock);
   #else
      CloseHandle(m_SendBlockLock);
      CloseHandle(m_SendBlockCond);
      CloseHandle(m_RecvDataLock);
      CloseHandle(m_RecvDataCond);
      ///CloseHandle(m_SendLock);
      ///CloseHandle(m_RecvLock);
      CloseHandle(m_AckLock);
      CloseHandle(m_ConnectionLock);

      CloseHandle(m_SerialLock);
   #endif
}

void CUDT::releaseSynch()
{
   #ifndef WIN32
      // wake up user calls
      pthread_mutex_lock(&m_SendBlockLock);
      pthread_cond_signal(&m_SendBlockCond);
      pthread_mutex_unlock(&m_SendBlockLock);

      ///pthread_mutex_lock(&m_SendLock);
      ///pthread_mutex_unlock(&m_SendLock);

      pthread_mutex_lock(&m_RecvDataLock);
      pthread_cond_signal(&m_RecvDataCond);
      pthread_mutex_unlock(&m_RecvDataLock);

      ///pthread_mutex_lock(&m_RecvLock);
      ///pthread_mutex_unlock(&m_RecvLock);

      pthread_mutex_lock(&m_SerialLock);
      pthread_mutex_unlock(&m_SerialLock);
   #else
      SetEvent(m_SendBlockCond);

      ///WaitForSingleObject(m_SendLock, INFINITE);
      ///ReleaseMutex(m_SendLock);

      SetEvent(m_RecvDataCond);

      ///WaitForSingleObject(m_RecvLock, INFINITE);
      ///ReleaseMutex(m_RecvLock);

      WaitForSingleObject(m_SerialLock, INFINITE);
      ReleaseMutex(m_SerialLock);
   #endif
}

void CUDT::sendCtrl(int pkttype, void* lparam, void* rparam, int size)
{
   CPacket ctrlpkt;

   switch (pkttype)
   {
   case 2: //010 - Acknowledgement
      {
      int32_t ack;

      // If there is no loss, the ACK is the current largest sequence number plus 1;
      // Otherwise it is the smallest sequence number in the receiver loss list.
      if (0 == m_pRcvLossList->getLossLength())
         ack = CSeqNo::incseq(m_iRcvCurrSeqNo);
      else
         ack = m_pRcvLossList->getFirstLostSeq();

      if (ack == m_iRcvLastAckAck)
         break;

      // send out a lite ACK
      // to save time on buffer processing and bandwidth/AS measurement, a lite ACK only feeds back an ACK number
      if (4 == size)
      {
    	 ctrlpkt.m_iID = m_PeerID;
         ctrlpkt.pack(pkttype, NULL, &ack, size);
         // Set MAC in secure mode
         if (m_pSecMod) {
        	 ctrlpkt.setMAC(&m_pSecKey[0], 16);
         }
         m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

         break;
      }

      uint64_t currtime;
      CTimer::rdtsc(currtime);

      // There are new received packets to acknowledge, update related information.
      if (CSeqNo::seqcmp(ack, m_iRcvLastAck) > 0)
      {
         int acksize = CSeqNo::seqoff(m_iRcvLastAck, ack);

         m_iRcvLastAck = ack;

         m_pRcvBuffer->ackData(acksize);

         // signal a waiting "recv" call if there is any data available
         #ifndef WIN32
            pthread_mutex_lock(&m_RecvDataLock);
            if (m_bSynRecving)
               pthread_cond_signal(&m_RecvDataCond);
            pthread_mutex_unlock(&m_RecvDataLock);
         #else
            if (m_bSynRecving)
               SetEvent(m_RecvDataCond);
         #endif

         // acknowledge any waiting epolls to read
         s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, true);

#ifdef EVPIPE_OSFD
         // trigger event pipe
         ///printf("%s.%s.%d, trigger Sent...", __FILE__, __FUNCTION__, __LINE__);
         feedOsfd();
         ///printf("done\n");
#endif
      }
      else if (ack == m_iRcvLastAck)
      {
         if ((currtime - m_ullLastAckTime) < ((m_iRTT + 4 * m_iRTTVar) * m_ullCPUFrequency))
            break;
      }
      else
         break;

      // Send out the ACK only if has not been received by the sender before
      if (CSeqNo::seqcmp(m_iRcvLastAck, m_iRcvLastAckAck) > 0)
      {
         int32_t data[6];

         m_iAckSeqNo = CAckNo::incack(m_iAckSeqNo);
         data[0] = m_iRcvLastAck;
         data[1] = m_iRTT;
         data[2] = m_iRTTVar;
         data[3] = m_pRcvBuffer->getAvailBufSize();
         // a minimum flow window of 2 is used, even if buffer is full, to break potential deadlock
         if (data[3] < 2)
            data[3] = 2;

         ctrlpkt.m_iID = m_PeerID;
         if (currtime - m_ullLastAckTime > m_ullSYNInt)
         {
            data[4] = m_pRcvTimeWindow->getPktRcvSpeed();
            data[5] = m_pRcvTimeWindow->getBandwidth();
            ctrlpkt.pack(pkttype, &m_iAckSeqNo, data, 24);

            CTimer::rdtsc(m_ullLastAckTime);
         }
         else
         {
            ctrlpkt.pack(pkttype, &m_iAckSeqNo, data, 16);
         }
         // Set MAC in secure mode
         if (m_pSecMod) {
        	 ctrlpkt.setMAC(&m_pSecKey[0], 16);
         }
         m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

         m_pACKWindow->store(m_iAckSeqNo, m_iRcvLastAck);

         ++ m_iSentACK;
         ++ m_iSentACKTotal;
      }

      break;
      }

   case 6: //110 - Acknowledgement of Acknowledgement
	  ctrlpkt.m_iID = m_PeerID;
      ctrlpkt.pack(pkttype, lparam);
      // Set MAC in secure mode
      if (m_pSecMod) {
     	 ctrlpkt.setMAC(&m_pSecKey[0], 16);
      }
      m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

      break;

   case 3: //011 - Loss Report
      {
      if (NULL != rparam)
      {
         ctrlpkt.m_iID = m_PeerID;
         if (1 == size)
         {
            // only 1 loss packet
            ctrlpkt.pack(pkttype, NULL, (int32_t *)rparam + 1, 4);
         }
         else
         {
            // more than 1 loss packets
            ctrlpkt.pack(pkttype, NULL, rparam, 8);
         }
         // Set MAC in secure mode
         if (m_pSecMod) {
        	 ctrlpkt.setMAC(&m_pSecKey[0], 16);
         }
         m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

         ++ m_iSentNAK;
         ++ m_iSentNAKTotal;
      }
      else if (m_pRcvLossList->getLossLength() > 0)
      {
         // this is periodically NAK report; make sure NAK cannot be sent back too often

         // read loss list from the local receiver loss list
         int32_t* data = new int32_t[m_iPayloadSize / 4];
         int losslen;
         m_pRcvLossList->getLossArray(data, losslen, m_iPayloadSize / 4);

         if (0 < losslen)
         {
        	ctrlpkt.m_iID = m_PeerID;
            ctrlpkt.pack(pkttype, NULL, data, losslen * 4);
            // Set MAC in secure mode
            if (m_pSecMod) {
           	    ctrlpkt.setMAC(&m_pSecKey[0], 16);
            }
            m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

            ++ m_iSentNAK;
            ++ m_iSentNAKTotal;
         }

         delete [] data;
      }

      // update next NAK time, which should wait enough time for the retansmission, but not too long
      m_ullNAKInt = (m_iRTT + 4 * m_iRTTVar) * m_ullCPUFrequency;
      int rcv_speed = m_pRcvTimeWindow->getPktRcvSpeed();
      if (rcv_speed > 0)
         m_ullNAKInt += (m_pRcvLossList->getLossLength() * 1000000ULL / rcv_speed) * m_ullCPUFrequency;
      if (m_ullNAKInt < m_ullMinNakInt)
         m_ullNAKInt = m_ullMinNakInt;

      break;
      }

   case 4: //100 - Congestion Warning
	  ctrlpkt.m_iID = m_PeerID;
      ctrlpkt.pack(pkttype);
      // Set MAC in secure mode
      if (m_pSecMod) {
     	 ctrlpkt.setMAC(&m_pSecKey[0], 16);
      }
      m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

      CTimer::rdtsc(m_ullLastWarningTime);

      break;

   case 1: //001 - Keep-alive
      ctrlpkt.m_iID = m_PeerID;
      ctrlpkt.m_iMsgNo = m_pCookie ^ m_iISN;
      ctrlpkt.pack(pkttype);
      // Set MAC in secure mode
      if (m_pSecMod) {
     	 ctrlpkt.setMAC(&m_pSecKey[0], 16);
      }
      m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);
 
      break;

   case 0: //000 - Handshake
      ctrlpkt.m_iID = m_PeerID;
      ctrlpkt.pack(pkttype, NULL, rparam, sizeof(CHandShake));
      m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

      break;

   case 5: //101 - Shutdown
      ctrlpkt.m_iID = m_PeerID;
      ctrlpkt.pack(pkttype);
      // Set MAC in secure mode
      if (m_pSecMod) {
     	 ctrlpkt.setMAC(&m_pSecKey[0], 16);
      }
      m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

      break;

   case 7: //111 - Msg drop request
      ctrlpkt.m_iID = m_PeerID;
      ctrlpkt.pack(pkttype, lparam, rparam, 8);
      // Set MAC in secure mode
      if (m_pSecMod) {
     	 ctrlpkt.setMAC(&m_pSecKey[0], 16);
      }
      m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

      break;

   case 8: //1000 - acknowledge the peer side a special error
      ctrlpkt.m_iID = m_PeerID;
      ctrlpkt.pack(pkttype, lparam);
      // Set MAC in secure mode
      if (m_pSecMod) {
     	 ctrlpkt.setMAC(&m_pSecKey[0], 16);
      }
      m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

      break;

   case 16383: //0x3FFF - Resevered for future use
      break;

   default:
      break;
   }
}

#include <time.h>

void CUDT::processCtrl(CPacket& ctrlpkt)
{
    // Check MAC in secure mode
	if (m_pSecMod) {
		if (!ctrlpkt.chkMAC(&m_pSecKey[0], 16)) {
#if 0            
			time_t rawtime;
			time(&rawtime);

			printf("%s DDOS attack, ctrlpkt MAC check failed.pkt.type:%d ", ctime(&rawtime), ctrlpkt.getType());

			// log attack
			if (m_iIPversion == AF_INET) {
				// IPv4
				int ip = ntohl(((sockaddr_in *)m_pPeerAddr)->sin_addr.s_addr);
				printf(" from ipv4: "
						"%d.%d.%d.%d\n",
						(ip>>24)&0xff, (ip>>16)&0xff, (ip>>8)&0xff, (ip>>0)&0xff);
			} else {
				// IPv6
				sockaddr_in6* a = (sockaddr_in6 *)m_pPeerAddr;
				printf(" from ipv6: "
						"%d.%d.%d.%d."
						"%d.%d.%d.%d."
						"%d.%d.%d.%d."
						"%d.%d.%d.%d\n",
						a->sin6_addr.s6_addr[15], a->sin6_addr.s6_addr[14], a->sin6_addr.s6_addr[13],a->sin6_addr.s6_addr[12],
						a->sin6_addr.s6_addr[11], a->sin6_addr.s6_addr[10], a->sin6_addr.s6_addr[9],a->sin6_addr.s6_addr[8],
						a->sin6_addr.s6_addr[7], a->sin6_addr.s6_addr[6], a->sin6_addr.s6_addr[5],a->sin6_addr.s6_addr[4],
						a->sin6_addr.s6_addr[3], a->sin6_addr.s6_addr[2], a->sin6_addr.s6_addr[1],a->sin6_addr.s6_addr[0]);
			}
#endif

			return;
		}
	}

   // Just heard from the peer, reset the expiration count.
   m_iEXPCount = 1;
   uint64_t currtime;
   CTimer::rdtsc(currtime);
   m_ullLastRspTime = currtime;

   switch (ctrlpkt.getType())
   {
   case 2: //010 - Acknowledgement
      {
      int32_t ack;

      // process a lite ACK
      if (4 == ctrlpkt.getLength())
      {
         ack = *(int32_t *)ctrlpkt.m_pcData;
         if (CSeqNo::seqcmp(ack, m_iSndLastAck) >= 0)
         {
            m_iFlowWindowSize -= CSeqNo::seqoff(m_iSndLastAck, ack);
            m_iSndLastAck = ack;
         }

         break;
      }

       // read ACK seq. no.
      ack = ctrlpkt.getAckSeqNo();

      // send ACK acknowledgement
      // number of ACK2 can be much less than number of ACK
      uint64_t now = CTimer::getTime();
      if ((currtime - m_ullSndLastAck2Time > (uint64_t)m_iSYNInterval) || (ack == m_iSndLastAck2))
      {
         sendCtrl(6, &ack);
         m_iSndLastAck2 = ack;
         m_ullSndLastAck2Time = now;
      }

      // Got data ACK
      ack = *(int32_t *)ctrlpkt.m_pcData;

      // check the validation of the ack
      if (CSeqNo::seqcmp(ack, CSeqNo::incseq(m_iSndCurrSeqNo)) > 0)
      {
         //this should not happen: attack or bug
         m_bBroken = true;
         m_iBrokenCounter = 0;
         break;
      }

      if (CSeqNo::seqcmp(ack, m_iSndLastAck) >= 0)
      {
         // Update Flow Window Size, must update before and together with m_iSndLastAck
         m_iFlowWindowSize = *((int32_t *)ctrlpkt.m_pcData + 3);
         m_iSndLastAck = ack;
      }

      // protect packet retransmission
      CGuard::enterCS(m_AckLock);

      int offset = CSeqNo::seqoff(m_iSndLastDataAck, ack);
      if (offset <= 0)
      {
         // discard it if it is a repeated ACK
         CGuard::leaveCS(m_AckLock);
         break;
      }

      // acknowledge the sending buffer
      m_pSndBuffer->ackData(offset);

      // record total time used for sending
      m_llSndDuration += currtime - m_llSndDurationCounter;
      m_llSndDurationTotal += currtime - m_llSndDurationCounter;
      m_llSndDurationCounter = currtime;

      // update sending variables
      m_iSndLastDataAck = ack;
      m_pSndLossList->remove(CSeqNo::decseq(m_iSndLastDataAck));

      CGuard::leaveCS(m_AckLock);

      #ifndef WIN32
         pthread_mutex_lock(&m_SendBlockLock);
         if (m_bSynSending)
            pthread_cond_signal(&m_SendBlockCond);
         pthread_mutex_unlock(&m_SendBlockLock);
      #else
         if (m_bSynSending)
            SetEvent(m_SendBlockCond);
      #endif

      // acknowledge any waiting epolls to write
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, true);

      // insert this socket to snd list if it is not on the list yet
      // notes: if it's high priority socket, then reschedule it immediately
      if (m_iQos)
   	   m_pSndQueue->m_pSndUList->update(this);
      else
   	   m_pSndQueue->m_pSndUList->update(this, false);

#ifdef EVPIPE_OSFD
      // trigger event pipe
      ///printf("%s.%s.%d, trigger Ack...", __FILE__, __FUNCTION__, __LINE__);
      feedOsfd();
      ///printf("done\n");
#endif

      // Update RTT
      //m_iRTT = *((int32_t *)ctrlpkt.m_pcData + 1);
      //m_iRTTVar = *((int32_t *)ctrlpkt.m_pcData + 2);
      int rtt = *((int32_t *)ctrlpkt.m_pcData + 1);
      m_iRTTVar = (m_iRTTVar * 3 + abs(rtt - m_iRTT)) >> 2;
      m_iRTT = (m_iRTT * 7 + rtt) >> 3;

      m_pCC->setRTT(m_iRTT);

      if (ctrlpkt.getLength() > 16)
      {
         // Update Estimated Bandwidth and packet delivery rate
         if (*((int32_t *)ctrlpkt.m_pcData + 4) > 0)
            m_iDeliveryRate = (m_iDeliveryRate * 7 + *((int32_t *)ctrlpkt.m_pcData + 4)) >> 3;

         if (*((int32_t *)ctrlpkt.m_pcData + 5) > 0)
            m_iBandwidth = (m_iBandwidth * 7 + *((int32_t *)ctrlpkt.m_pcData + 5)) >> 3;

         m_pCC->setRcvRate(m_iDeliveryRate);
         m_pCC->setBandwidth(m_iBandwidth);
      }

      m_pCC->onACK(ack);
      CCUpdate();

      ++ m_iRecvACK;
      ++ m_iRecvACKTotal;

      break;
   }

   case 6: //110 - Acknowledgement of Acknowledgement
      {
      int32_t ack;
      int rtt = -1;

      // update RTT
      rtt = m_pACKWindow->acknowledge(ctrlpkt.getAckSeqNo(), ack);
      if (rtt <= 0)
         break;

      //if increasing delay detected...
      //   sendCtrl(4);

      // RTT EWMA
      m_iRTTVar = (m_iRTTVar * 3 + abs(rtt - m_iRTT)) >> 2;
      m_iRTT = (m_iRTT * 7 + rtt) >> 3;

      m_pCC->setRTT(m_iRTT);

      // update last ACK that has been received by the sender
      if (CSeqNo::seqcmp(ack, m_iRcvLastAckAck) > 0)
         m_iRcvLastAckAck = ack;

      break;
      }

   case 3: //011 - Loss Report
      {
      int32_t* losslist = (int32_t *)(ctrlpkt.m_pcData);

      m_pCC->onLoss(losslist, ctrlpkt.getLength() / 4);
      CCUpdate();

      bool secure = true;

      // decode loss list message and insert loss into the sender loss list
      for (int i = 0, n = (int)(ctrlpkt.getLength() / 4); i < n; ++ i)
      {
         if (0 != (losslist[i] & 0x80000000))
         {
            if ((CSeqNo::seqcmp(losslist[i] & 0x7FFFFFFF, losslist[i + 1]) > 0) || (CSeqNo::seqcmp(losslist[i + 1], m_iSndCurrSeqNo) > 0))
            {
               // seq_a must not be greater than seq_b; seq_b must not be greater than the most recent sent seq
               secure = false;
               break;
            }

            int num = 0;
            if (CSeqNo::seqcmp(losslist[i] & 0x7FFFFFFF, m_iSndLastAck) >= 0)
               num = m_pSndLossList->insert(losslist[i] & 0x7FFFFFFF, losslist[i + 1]);
            else if (CSeqNo::seqcmp(losslist[i + 1], m_iSndLastAck) >= 0)
               num = m_pSndLossList->insert(m_iSndLastAck, losslist[i + 1]);

            m_iTraceSndLoss += num;
            m_iSndLossTotal += num;

            ++ i;
         }
         else if (CSeqNo::seqcmp(losslist[i], m_iSndLastAck) >= 0)
         {
            if (CSeqNo::seqcmp(losslist[i], m_iSndCurrSeqNo) > 0)
            {
               //seq_a must not be greater than the most recent sent seq
               secure = false;
               break;
            }

            int num = m_pSndLossList->insert(losslist[i], losslist[i]);

            m_iTraceSndLoss += num;
            m_iSndLossTotal += num;
         }
      }

      if (!secure)
      {
         //this should not happen: attack or bug
         m_bBroken = true;
         m_iBrokenCounter = 0;
         break;
      }

      // the lost packet (retransmission) should be sent out immediately
      m_pSndQueue->m_pSndUList->update(this);

      ++ m_iRecvNAK;
      ++ m_iRecvNAKTotal;

      break;
      }

   case 4: //100 - Delay Warning
      // One way packet delay is increasing, so decrease the sending rate
      m_ullInterval = (uint64_t)ceil(m_ullInterval * 1.125);
      m_iLastDecSeq = m_iSndCurrSeqNo;

      break;

   case 1: //001 - Keep-alive
      // The only purpose of keep-alive packet is to tell that the peer is still alive
      // nothing needs to be done.

      break;

   case 0: //000 - Handshake
      {
      CHandShake req;
      req.deserialize(ctrlpkt.m_pcData, ctrlpkt.getLength());
      if ((req.m_iReqType > 0) || (m_bRendezvous && (req.m_iReqType != -2)))
      {
         // The peer side has not received the handshake message, so it keeps querying
         // resend the handshake packet

         CHandShake initdata;
         initdata.m_iISN = m_iISN;
         initdata.m_iMSS = m_iMSS;
         initdata.m_iFlightFlagSize = m_iFlightFlagSize;
         initdata.m_iReqType = (!m_bRendezvous) ? -1 : -2;
         initdata.m_iID = m_SocketID;

         char* hs = new char [m_iPayloadSize];
         int hs_size = m_iPayloadSize;
         initdata.serialize(hs, hs_size);
         sendCtrl(0, NULL, hs, hs_size);
         delete [] hs;
      }

      break;
      }

   case 5: //101 - Shutdown
      m_bShutdown = true;
      m_bClosing = true;
      m_bBroken = true;
      m_iBrokenCounter = 60;

#ifndef EVPIPE_OSFD
      // Signal the sender and recver if they are waiting for data.
      ///printf("%s.%s.%d\n", __FILE__, __FUNCTION__, __LINE__);
      releaseSynch();
      ///printf("%s.%s.%d\n", __FILE__, __FUNCTION__, __LINE__);
#endif

      CTimer::triggerEvent();

#ifdef EVPIPE_OSFD
      // trigger event pipe
      // TBD... verify if need to disable it to avoid error event deadlock, when close socket
      ///printf("%s.%s.%d, trigger Shutdown...\n", __FILE__, __FUNCTION__, __LINE__);
      feedOsfd();
      ///printf("done\n");
#endif

      break;

   case 7: //111 - Msg drop request
      m_pRcvBuffer->dropMsg(ctrlpkt.getMsgSeq());
      m_pRcvLossList->remove(*(int32_t*)ctrlpkt.m_pcData, *(int32_t*)(ctrlpkt.m_pcData + 4));

      // move forward with current recv seq no.
      if ((CSeqNo::seqcmp(*(int32_t*)ctrlpkt.m_pcData, CSeqNo::incseq(m_iRcvCurrSeqNo)) <= 0)
         && (CSeqNo::seqcmp(*(int32_t*)(ctrlpkt.m_pcData + 4), m_iRcvCurrSeqNo) > 0))
      {
         m_iRcvCurrSeqNo = *(int32_t*)(ctrlpkt.m_pcData + 4);
      }

      break;

   case 8: // 1000 - An error has happened to the peer side
      //int err_type = packet.getAddInfo();

      // currently only this error is signalled from the peer side
      // if recvfile() failes (e.g., due to disk fail), blcoked sendfile/send should return immediately
      // giving the app a chance to fix the issue

      m_bPeerHealth = false;

      break;

   case 32767: //0x7FFF - reserved and user defined messages
      m_pCC->processCustomMsg(&ctrlpkt);
      CCUpdate();

      break;

   default:
      break;
   }
}

int CUDT::packData(CPacket& packet, uint64_t& ts)
{
   int payload = 0;
   bool probe = false;

   uint64_t entertime;
   CTimer::rdtsc(entertime);

   if ((0 != m_ullTargetTime) && (entertime > m_ullTargetTime))
      m_ullTimeDiff += entertime - m_ullTargetTime;

   // Loss retransmission always has higher priority.
   if ((packet.m_iSeqNo = m_pSndLossList->getLostSeq()) >= 0)
   {
      // protect m_iSndLastDataAck from updating by ACK processing
      CGuard ackguard(m_AckLock);

      int offset = CSeqNo::seqoff(m_iSndLastDataAck, packet.m_iSeqNo);
      if (offset < 0)
         return 0;

      int msglen;

      payload = m_pSndBuffer->readData(&(packet.m_pcData), offset, packet.m_iMsgNo, msglen);

      if (-1 == payload)
      {
         int32_t seqpair[2];
         seqpair[0] = packet.m_iSeqNo;
         seqpair[1] = CSeqNo::incseq(seqpair[0], msglen);
         sendCtrl(7, &packet.m_iMsgNo, seqpair, 8);

         // only one msg drop request is necessary
         m_pSndLossList->remove(seqpair[1]);

         // skip all dropped packets
         if (CSeqNo::seqcmp(m_iSndCurrSeqNo, CSeqNo::incseq(seqpair[1])) < 0)
             m_iSndCurrSeqNo = CSeqNo::incseq(seqpair[1]);

         return 0;
      }
      else if (0 == payload)
         return 0;

      ++ m_iTraceRetrans;
      ++ m_iRetransTotal;
   }
   else
   {
      // If no loss, pack a new packet.

      // check congestion/flow window limit
      int cwnd = (m_iFlowWindowSize < (int)m_dCongestionWindow) ? m_iFlowWindowSize : (int)m_dCongestionWindow;
      if (cwnd >= CSeqNo::seqlen(m_iSndLastAck, CSeqNo::incseq(m_iSndCurrSeqNo)))
      {
         if (0 != (payload = m_pSndBuffer->readData(&(packet.m_pcData), packet.m_iMsgNo)))
         {
            m_iSndCurrSeqNo = CSeqNo::incseq(m_iSndCurrSeqNo);
            m_pCC->setSndCurrSeqNo(m_iSndCurrSeqNo);

            packet.m_iSeqNo = m_iSndCurrSeqNo;

            // every 16 (0xF) packets, a packet pair is sent
            if (0 == (packet.m_iSeqNo & 0xF))
               probe = true;
         }
         else
         {
            m_ullTargetTime = 0;
            m_ullTimeDiff = 0;
            ts = 0;
            return 0;
         }
      }
      else
      {
         m_ullTargetTime = 0;
         m_ullTimeDiff = 0;
         ts = 0;
         return 0;
      }
   }

   packet.m_iTimeStamp = int(CTimer::getTime() - m_StartTime);
   packet.m_iID = m_PeerID;
   packet.setLength(payload);
   // Set MAC in secure mode
   if (m_pSecMod > 1)
   {
       packet.setMAC(&m_pSecKey[0], 16);
   }

   m_pCC->onPktSent(&packet);
   //m_pSndTimeWindow->onPktSent(packet.m_iTimeStamp);

   ++ m_llTraceSent;
   ++ m_llSentTotal;

   if (probe)
   {
      // sends out probing packet pair
      ts = entertime;
      probe = false;
   }
   else
   {
      #ifndef NO_BUSY_WAITING
         ts = entertime + m_ullInterval;
      #else
         if (m_ullTimeDiff >= m_ullInterval)
         {
            ts = entertime;
            m_ullTimeDiff -= m_ullInterval;
         }
         else
         {
            ts = entertime + m_ullInterval - m_ullTimeDiff;
            m_ullTimeDiff = 0;
         }
      #endif
   }

   m_ullTargetTime = ts;

   return payload;
}

int CUDT::processData(CUnit* unit)
{
   CPacket& packet = unit->m_Packet;

   // Check MAC in secure mode
   if (m_pSecMod > 1)
   {
       if (!packet.chkMAC(&m_pSecKey[0], 16))
       {
#if 0
			time_t rawtime;
			time(&rawtime);

			printf("%s DDOS attack, datapkt MAC check failed. ", ctime(&rawtime));

			// log attack
			if (m_iIPversion == AF_INET) {
				// IPv4
				int ip = ntohl(((sockaddr_in *)m_pPeerAddr)->sin_addr.s_addr);
				printf(" from ipv4: "
						"%d.%d.%d.%d\n",
						(ip>>24)&0xff, (ip>>16)&0xff, (ip>>8)&0xff, (ip>>0)&0xff);
			} else {
				// IPv6
				sockaddr_in6* a = (sockaddr_in6 *)m_pPeerAddr;
				printf(" from ipv6: "
						"%d.%d.%d.%d."
						"%d.%d.%d.%d."
						"%d.%d.%d.%d."
						"%d.%d.%d.%d\n",
						a->sin6_addr.s6_addr[15], a->sin6_addr.s6_addr[14], a->sin6_addr.s6_addr[13],a->sin6_addr.s6_addr[12],
						a->sin6_addr.s6_addr[11], a->sin6_addr.s6_addr[10], a->sin6_addr.s6_addr[9],a->sin6_addr.s6_addr[8],
						a->sin6_addr.s6_addr[7], a->sin6_addr.s6_addr[6], a->sin6_addr.s6_addr[5],a->sin6_addr.s6_addr[4],
						a->sin6_addr.s6_addr[3], a->sin6_addr.s6_addr[2], a->sin6_addr.s6_addr[1],a->sin6_addr.s6_addr[0]);
			}
#endif

           return -1;
       }
   }

   // Just heard from the peer, reset the expiration count.
   m_iEXPCount = 1;
   uint64_t currtime;
   CTimer::rdtsc(currtime);
   m_ullLastRspTime = currtime;

   m_pCC->onPktReceived(&packet);
   ++ m_iPktCount;
   // update time information
   m_pRcvTimeWindow->onPktArrival();

   // check if it is probing packet pair
   if (0 == (packet.m_iSeqNo & 0xF))
      m_pRcvTimeWindow->probe1Arrival();
   else if (1 == (packet.m_iSeqNo & 0xF))
      m_pRcvTimeWindow->probe2Arrival();

   ++ m_llTraceRecv;
   ++ m_llRecvTotal;

   int32_t offset = CSeqNo::seqoff(m_iRcvLastAck, packet.m_iSeqNo);
   if ((offset < 0) || (offset >= m_pRcvBuffer->getAvailBufSize()))
      return -1;

   if (m_pRcvBuffer->addData(unit, offset) < 0)
      return -1;

   // Loss detection.
   if (CSeqNo::seqcmp(packet.m_iSeqNo, CSeqNo::incseq(m_iRcvCurrSeqNo)) > 0)
   {
      // If loss found, insert them to the receiver loss list
      m_pRcvLossList->insert(CSeqNo::incseq(m_iRcvCurrSeqNo), CSeqNo::decseq(packet.m_iSeqNo));

      // pack loss list for NAK
      int32_t lossdata[2];
      lossdata[0] = CSeqNo::incseq(m_iRcvCurrSeqNo) | 0x80000000;
      lossdata[1] = CSeqNo::decseq(packet.m_iSeqNo);

      // Generate loss report immediately.
      sendCtrl(3, NULL, lossdata, (CSeqNo::incseq(m_iRcvCurrSeqNo) == CSeqNo::decseq(packet.m_iSeqNo)) ? 1 : 2);

      int loss = CSeqNo::seqlen(m_iRcvCurrSeqNo, packet.m_iSeqNo) - 2;
      m_iTraceRcvLoss += loss;
      m_iRcvLossTotal += loss;
   }

   // This is not a regular fixed size packet...   
   //an irregular sized packet usually indicates the end of a message, so send an ACK immediately   
   if (packet.getLength() != m_iPayloadSize)   
      CTimer::rdtsc(m_ullNextACKTime); 

   // Update the current largest sequence number that has been received.
   // Or it is a retransmitted packet, remove it from receiver loss list.
   if (CSeqNo::seqcmp(packet.m_iSeqNo, m_iRcvCurrSeqNo) > 0)
      m_iRcvCurrSeqNo = packet.m_iSeqNo;
   else
      m_pRcvLossList->remove(packet.m_iSeqNo);

   return 0;
}

int CUDT::listen(sockaddr* addr, CPacket& packet)
{
   if (m_bClosing)
      return 1002;

   if (packet.getLength() != CHandShake::m_iContentSize)
      return 1004;

   CHandShake hs;
   hs.deserialize(packet.m_pcData, packet.getLength());

   // SYN cookie
   char clienthost[NI_MAXHOST];
   char clientport[NI_MAXSERV];
   getnameinfo(addr, (AF_INET == m_iIPversion) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6), clienthost, sizeof(clienthost), clientport, sizeof(clientport), NI_NUMERICHOST | NI_NUMERICSERV);
   int64_t timestamp = (CTimer::getTime() - m_StartTime) / 60000000; // secret changes every one minute
   stringstream cookiestr;
   cookiestr << clienthost << ":" << clientport << ":" << timestamp;
   unsigned char cookie[16];
   CMD5::compute(cookiestr.str().c_str(), cookie);

   if (1 == hs.m_iReqType)
   {
      hs.m_iCookie = *(int*)cookie;
      packet.m_iID = hs.m_iID;
      int size = packet.getLength();
      hs.serialize(packet.m_pcData, size);
      m_pSndQueue->sendto(addr, packet);
      return 0;
   }
   else
   {
      if (hs.m_iCookie != *(int*)cookie)
      {
         timestamp --;
         cookiestr << clienthost << ":" << clientport << ":" << timestamp;
         CMD5::compute(cookiestr.str().c_str(), cookie);

         if (hs.m_iCookie != *(int*)cookie)
            return -1;
      }
   }

   int32_t id = hs.m_iID;

   // When a peer side connects in...
   if ((1 == packet.getFlag()) && (0 == packet.getType()))
   {
      if ((hs.m_iVersion != m_iVersion) || (hs.m_iType != m_iSockType))
      {
         // mismatch, reject the request
         hs.m_iReqType = 1002;
         int size = CHandShake::m_iContentSize;
         hs.serialize(packet.m_pcData, size);
         packet.m_iID = id;
         m_pSndQueue->sendto(addr, packet);
      }
      else
      {
         int result = s_UDTUnited.newConnection(m_SocketID, addr, &hs);
         if (result == -1)
            hs.m_iReqType = 1002;

         // send back a response if connection failed or connection already existed
         // new connection response should be sent in connect()
         if (result != 1)
         {
            int size = CHandShake::m_iContentSize;
            hs.serialize(packet.m_pcData, size);
            packet.m_iID = id;
            m_pSndQueue->sendto(addr, packet);
         }
         else
         {
            // a new connection has been created, enable epoll for write 
            s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, true);

#ifdef EVPIPE_OSFD
            // trigger event pipe
            ///printf("%s.%s.%d, trigger Listened...", __FILE__, __FUNCTION__, __LINE__);
            feedOsfd();
            ///printf("done\n");
#endif
         }
      }
   }

   return hs.m_iReqType;
}

void CUDT::checkTimers()
{
   ///printf("checkTimers\n");

   // update CC parameters
   CCUpdate();
   //uint64_t minint = (uint64_t)(m_ullCPUFrequency * m_pSndTimeWindow->getMinPktSndInt() * 0.9);
   //if (m_ullInterval < minint)
   //   m_ullInterval = minint;

   uint64_t currtime;
   CTimer::rdtsc(currtime);

   if ((currtime > m_ullNextACKTime) || ((m_pCC->m_iACKInterval > 0) && (m_pCC->m_iACKInterval <= m_iPktCount)))
   {
      // ACK timer expired or ACK interval is reached

      sendCtrl(2);
      CTimer::rdtsc(currtime);
      if (m_pCC->m_iACKPeriod > 0)
         m_ullNextACKTime = currtime + m_pCC->m_iACKPeriod * m_ullCPUFrequency;
      else
         m_ullNextACKTime = currtime + m_ullACKInt;

      m_iPktCount = 0;
      m_iLightACKCount = 1;
   }
   else if (m_iSelfClockInterval * m_iLightACKCount <= m_iPktCount)
   {
      //send a "light" ACK
      sendCtrl(2, NULL, NULL, 4);
      ++ m_iLightACKCount;
   }

   // we are not sending back repeated NAK anymore and rely on the sender's EXP for retransmission
   //if ((m_pRcvLossList->getLossLength() > 0) && (currtime > m_ullNextNAKTime))
   //{
   //   // NAK timer expired, and there is loss to be reported.
   //   sendCtrl(3);
   //
   //   CTimer::rdtsc(currtime);
   //   m_ullNextNAKTime = currtime + m_ullNAKInt;
   //}

   uint64_t next_exp_time;
   if (m_pCC->m_bUserDefinedRTO)
      next_exp_time = m_ullLastRspTime + m_pCC->m_iRTO * m_ullCPUFrequency;
   else
   {
      uint64_t exp_int = (m_iEXPCount * (m_iRTT + 4 * m_iRTTVar) + m_iSYNInterval) * m_ullCPUFrequency;
      if (exp_int < m_iEXPCount * m_ullMinExpInt)
         exp_int = m_iEXPCount * m_ullMinExpInt;
      next_exp_time = m_ullLastRspTime + exp_int;
   }

   if (currtime > next_exp_time)
   {
      // Haven't receive any information from the peer, is it dead?!
      // timeout: at least 16 expirations and must be greater than 10 seconds
      if ((m_iEXPCount > 16) && (currtime - m_ullLastRspTime > 5000000 * m_ullCPUFrequency))
      ///if ((m_iEXPCount > 32) && (currtime - m_ullLastRspTime > 10000000 * m_ullCPUFrequency))
      {
         //
         // Connection is broken. 
         // UDT does not signal any information about this instead of to stop quietly.
         // Application will detect this when it calls any UDT methods next time.
         //
         m_bClosing = true;
         m_bBroken = true;
         m_iBrokenCounter = 30;

         // update snd U list to remove this socket
         m_pSndQueue->m_pSndUList->update(this);

#ifndef EVPIPE_OSFD
         releaseSynch();
#endif

         // app can call any UDT API to learn the connection_broken error
         s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN | UDT_EPOLL_OUT | UDT_EPOLL_ERR, true);

         CTimer::triggerEvent();

#ifdef EVPIPE_OSFD
         // trigger event pipe right here
         ///printf("%s.%s.%d, trigger Broken...", __FILE__, __FUNCTION__, __LINE__);
         feedOsfd();
         ///printf("done\n");
#endif

         return;
      }

      // sender: Insert all the packets sent after last received acknowledgement into the sender loss list.
      // recver: Send out a keep-alive packet
      if (m_pSndBuffer->getCurrBufSize() > 0)
      {
         if ((CSeqNo::incseq(m_iSndCurrSeqNo) != m_iSndLastAck) && (m_pSndLossList->getLossLength() == 0))
         {
            // resend all unacknowledged packets on timeout, but only if there is no packet in the loss list
            int32_t csn = m_iSndCurrSeqNo;
            int num = m_pSndLossList->insert(m_iSndLastAck, csn);
            m_iTraceSndLoss += num;
            m_iSndLossTotal += num;
         }

         m_pCC->onTimeout();
         CCUpdate();

         // immediately restart transmission
         m_pSndQueue->m_pSndUList->update(this);
      }
      else
      {
    	 sendCtrl(1);
      }

      ++ m_iEXPCount;
      // Reset last response time since we just sent a heart-beat.
      CTimer::rdtsc(currtime);
      m_ullLastRspTime = currtime;
      ///printf("%s.%s.%d, expiration ... %d:%d\n", __FILE__, __FUNCTION__, __LINE__, m_SocketID, m_iEXPCount);
   }

#ifdef EVPIPE_OSFD
	// check available recv/send/listening event every timers
	// notes: timer is 10ms by now
	if ((m_bConnected && (((m_pRcvBuffer->getRcvDataSize() > 0) && (m_iSockType == UDT_STREAM)) ||
		              ((m_pRcvBuffer->getRcvMsgNum() > 0) && (m_iSockType == UDT_DGRAM)))) ||
            (m_bListening && (m_pCUDTSocket->m_pQueuedSockets->size() > 0)))
	{
            ///printf("%s.%s.%d, trigger recv/listen ...", __FILE__, __FUNCTION__, __LINE__);
            feedOsfd();
            ///printf("done\n");
	} 

        if (m_bConnected && (m_iSndBufSize > m_pSndBuffer->getCurrBufSize()))
	{
            ///printf("%s.%s.%d, trigger send ...", __FILE__, __FUNCTION__, __LINE__);
            ///feedOsfd();
            ///printf("done\n");
	}
#endif
}

void CUDT::addEPoll(const int eid)
{
   CGuard::enterCS(s_UDTUnited.m_EPoll.m_EPollLock);
   m_sPollID.insert(eid);
   CGuard::leaveCS(s_UDTUnited.m_EPoll.m_EPollLock);

   if (!m_bConnected || m_bBroken || m_bClosing)
      return;

   if (((UDT_STREAM == m_iSockType) && (m_pRcvBuffer->getRcvDataSize() > 0)) ||
      ((UDT_DGRAM == m_iSockType) && (m_pRcvBuffer->getRcvMsgNum() > 0)))
   {
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, true);
   }
   if (m_iSndBufSize > m_pSndBuffer->getCurrBufSize())
   {
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, true);
   }
}

void CUDT::removeEPoll(const int eid)
{
   // clear IO events notifications;
   // since this happens after the epoll ID has been removed, they cannot be set again
   set<int> remove;
   remove.insert(eid);
   s_UDTUnited.m_EPoll.update_events(m_SocketID, remove, UDT_EPOLL_IN | UDT_EPOLL_OUT, false);

   CGuard::enterCS(s_UDTUnited.m_EPoll.m_EPollLock);
   m_sPollID.erase(eid);
   CGuard::leaveCS(s_UDTUnited.m_EPoll.m_EPollLock);
}
