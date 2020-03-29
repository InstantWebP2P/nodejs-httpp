/*****************************************************************************
Copyright (c) 2010 - 2012, Tom Zhou at iwebpp@gmail.com.
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

* Neither the name of the creator nor the names of its contributors may be used to
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
   Tom Zhou, last updated 05/18/2012
*****************************************************************************/

#include "udt.h"
#include "udtc.h"
#include "common.h"
#include "cc_cwrapper.h" // congestion control algorithms


extern "C" {

int udt_startup()
{
    int rc;

    rc = UDT::startup();
    if (rc == UDT::ERROR) {
        // error happen
        return -1;
    } else {
        return 0;
    }
}

int udt_cleanup()
{
    int rc;

    rc = UDT::cleanup();
    if (rc == UDT::ERROR) {
        // error happen
        return -1;
    } else {
        return 0;
    }
}

UDTSOCKET udt_socket(int af, int type, int protocol)
{
	UDTSOCKET rc;

    rc = UDT::socket(af, type, protocol);
    if (rc == UDT::INVALID_SOCK) {
        // error happen
        return UDT_INVALID_SOCK;
    } else {
        return rc;
    }
}

int udt_bind(UDTSOCKET u, const struct sockaddr * name, int namelen)
{
    int rc;

    rc = UDT::bind(u, name, namelen);
    if (rc == UDT::ERROR) {
        // error happen
        return -1;
    } else {
        return 0;
    }
}

int udt_bind2(UDTSOCKET u, UDPSOCKET udpsock)
{
    int rc;

    rc = UDT::bind2(u, udpsock);
    if (rc == UDT::ERROR) {
        // error happen
        return -1;
    } else {
        return 0;
    }
}

int udt_listen(UDTSOCKET u, int backlog)
{
    int rc;

    rc = UDT::listen(u, backlog);
    if (rc == UDT::ERROR) {
        // error happen
        return -1;
    } else {
        return 0;
    }
}

UDTSOCKET udt_accept(UDTSOCKET u, struct sockaddr * addr, int * addrlen)
{
	UDTSOCKET rc = -1;

    rc = UDT::accept(u, addr, addrlen);
    if (rc == UDT::INVALID_SOCK) {
        // error happen
        return -1;
    } else {
        return rc;
    }
}

int udt_punchhole(UDTSOCKET u, const struct sockaddr * name, int namelen, int from, int to)
{
    int rc;

    rc = UDT::punchhole(u, name, namelen, from, to);
    if (rc == UDT::ERROR) {
        // error happen
        return -1;
    } else {
        return 0;
    }
}

int udt_connect(UDTSOCKET u, const struct sockaddr * name, int namelen)
{
    int rc;

    rc = UDT::connect(u, name, namelen);
    if (rc == UDT::ERROR) {
        // error happen
        return -1;
    } else {
        return 0;
    }
}

int udt_close(UDTSOCKET u)
{
	int rc;
	int st = udt_getsockstate(u);

	///printf("%s.%d,u:%d,state:%d\n", __FUNCTION__, __LINE__, u, st);
	if ((st == UDT_NONEXIST) ||
		(st == UDT_CLOSED)   ||
		(st == UDT_CLOSING)  ||
		(st == UDT_BROKEN)) {
		// nothing to do on broken, closing, closed or not existed socket
		return 0;
	} else {
		rc = UDT::close(u);
		///printf("%s.%d,u:%d,state:%d\n", __FUNCTION__, __LINE__, u, st);
		if (rc == UDT::ERROR) {
			// error happen
			return -1;
		} else {
			return 0;
		}
	}
}

int udt_getpeername(UDTSOCKET u, struct sockaddr * name, int * namelen)
{
    int rc;

    rc = UDT::getpeername(u, name, namelen);
    if (rc == UDT::ERROR) {
        // error happen
        return -1;
    } else {
        return 0;
    }
}

int udt_getsockname(UDTSOCKET u, struct sockaddr * name, int * namelen)
{
    int rc;

    rc = UDT::getsockname(u, name, namelen);
    if (rc == UDT::ERROR) {
        // error happen
        return -1;
    } else {
        return 0;
    }
}

int udt_getsockopt(UDTSOCKET u, int level, int optname, void * optval, int * optlen)
{
    int rc;
    // process bool option
    if ((optname == UDT_UDT_SNDSYN)    || (optname == UDT_UDT_SNDSYN) || (optname == UDT_UDT_RENDEZVOUS) ||
        (optname == UDT_UDT_REUSEADDR) || (optname == UDT_UDT_REUSEABLE))
    {
        bool flag = false;
        rc = UDT::getsockopt(u, level, (UDT::SOCKOPT)optname, &flag, optlen);
        *(int *)optval = flag ? 1 : 0;
        *optlen = sizeof(int);
    }
    else 
    {
        rc = UDT::getsockopt(u, level, (UDT::SOCKOPT)optname, optval, optlen);
    }
    if (rc == UDT::ERROR) {
        // error happen
        return -1;
    } else {
        return 0;
    }
}

int udt_setsockopt(UDTSOCKET u, int level, int optname, const void * optval, int optlen)
{
	int rc;

	// process bool option
    if ((optname == UDT_UDT_SNDSYN)    || (optname == UDT_UDT_SNDSYN) || (optname == UDT_UDT_RENDEZVOUS) ||
        (optname == UDT_UDT_REUSEADDR) || (optname == UDT_UDT_REUSEABLE))
    {
        bool flag = (*(int *)optval) ? true : false;
		rc = UDT::setsockopt(u, level, (UDT::SOCKOPT)optname, &flag, sizeof(flag));
    }
    else
    {
        rc = UDT::setsockopt(u, level, (UDT::SOCKOPT)optname, optval, optlen);
    }
    if (rc == UDT::ERROR) {
		// error happen
		return -1;
	} else {
		return 0;
	}
}

int udt_send(UDTSOCKET u, const char * buf, int len, int flags)
{
    int rc;

    rc = UDT::send(u, buf, len, flags);
    if (rc == UDT::ERROR) {
        // error happen
        return -1;
    } else {
        return rc;
    }
}

int udt_recv(UDTSOCKET u, char * buf, int len, int flags)
{
    int rc;

    rc = UDT::recv(u, buf, len, flags);
    if (rc == UDT::ERROR) {
        // error happen
        return -1;
    } else {
        return rc;
    }
}

int udt_sendmsg(UDTSOCKET u, const char * buf, int len, int ttl, int inorder)
{
    int rc;

    rc = UDT::sendmsg(u, buf, len, ttl, inorder);
    if (rc == UDT::ERROR) {
        // error happen
        return -1;
    } else {
        return rc;
    }
}

int udt_recvmsg(UDTSOCKET u, char * buf, int len)
{
    int rc;

    rc = UDT::recvmsg(u, buf, len);
    if (rc == UDT::ERROR) {
        // error happen
        return -1;
    } else {
        return rc;
    }
}

int64_t udt_sendfile2(UDTSOCKET u, const char* path, int64_t* offset, int64_t size, int block)
{
	int64_t rc;

    rc = UDT::sendfile2(u, path, offset, size, block);
    if (rc == UDT::ERROR) {
        // error happen
        return -1;
    } else {
        return rc;
    }
}

int64_t udt_recvfile2(UDTSOCKET u, const char* path, int64_t* offset, int64_t size, int block)
{
	int64_t rc;

    rc = UDT::recvfile2(u, path, offset, size, block);
    if (rc == UDT::ERROR) {
        // error happen
        return -1;
    } else {
        return rc;
    }
}

const char * udt_getlasterror_desc()
{
    return UDT::getlasterror().getErrorMessage();
}

int udt_getlasterror_code()
{
    return UDT::getlasterror().getErrorCode();
}

void udt_clearlasterror()
{
    UDT::getlasterror().clear();
}

int udt_perfmon(UDTSOCKET u, UDT_TRACEINFO * perf, int clear)
{
    int rc;

    rc = UDT::perfmon(u, (UDT::TRACEINFO *)perf, clear);
    if (rc == UDT::ERROR) {
        // error happen
        return -1;
    } else {
        return 0;
    }
}

int udt_getsockstate(UDTSOCKET u)
{
	switch (UDT::getsockstate(u)) {
	case       INIT: return UDT_INIT;
	case     OPENED: return UDT_OPENED;
	case  LISTENING: return UDT_LISTENING;
	case CONNECTING: return UDT_CONNECTING;
	case  CONNECTED: return UDT_CONNECTED;
	case     BROKEN: return UDT_BROKEN;
	case    CLOSING: return UDT_CLOSING;
	case     CLOSED: return UDT_CLOSED;
	case   NONEXIST: return UDT_NONEXIST;
	        default: return UDT_NONEXIST;
	}
}

// congestion control algorithm
int udt_setccc(UDTSOCKET u, int ccc)
{
	int rc;
	CCCFactory<CTCP> tcp;
	CCCFactory<CUDTCC> udt;
	CCCFactory<CUDPBlast> blast;

	switch (ccc) {
	case UDT_CCC_TCP:
		rc = UDT::setsockopt(u, 0, UDT_CC, &tcp, sizeof(tcp));
		break;

	case UDT_CCC_BLAST:
		rc = UDT::setsockopt(u, 0, UDT_CC, &blast, sizeof(blast));
		break;

	case UDT_CCC_UDT:
	default:
		rc = UDT::setsockopt(u, 0, UDT_CC, &udt, sizeof(udt));
		break;
	}

	if (rc == UDT::ERROR) {
		// error happen
		return -1;
	} else {
		return 0;
	}
}

// event mechanism
/*
UDT_API int epoll_create();
UDT_API int epoll_add_usock(int eid, UDTSOCKET u, const int* events = NULL);
UDT_API int epoll_add_ssock(int eid, SYSSOCKET s, const int* events = NULL);
UDT_API int epoll_remove_usock(int eid, UDTSOCKET u);
UDT_API int epoll_remove_ssock(int eid, SYSSOCKET s);
UDT_API int epoll_wait(int eid, std::set<UDTSOCKET>* readfds, std::set<UDTSOCKET>* writefds, int64_t msTimeOut,
                       std::set<SYSSOCKET>* lrfds = NULL, std::set<SYSSOCKET>* wrfds = NULL);
UDT_API int epoll_wait2(int eid, UDTSOCKET* readfds, int* rnum, UDTSOCKET* writefds, int* wnum, int64_t msTimeOut,
                        SYSSOCKET* lrfds = NULL, int* lrnum = NULL, SYSSOCKET* lwfds = NULL, int* lwnum = NULL);
UDT_API int epoll_release(int eid);
 */

int udt_epoll_create()
{
    int rc;

    rc = UDT::epoll_create();
    if (rc == UDT::ERROR) {
        // error happen
        return -1;
    } else {
        // success
        return rc;
    }
}

int udt_epoll_add_usock(int eid, UDTSOCKET u, const int * events)
{
	int rc;

	// call UDT native function
	int udt_ev = 0;

	if (events) {
		if (*events & UDT_UDT_EPOLL_IN)  udt_ev |= UDT_EPOLL_IN;
		if (*events & UDT_UDT_EPOLL_OUT) udt_ev |= UDT_EPOLL_OUT;
		if (*events & UDT_UDT_EPOLL_ERR) udt_ev |= UDT_EPOLL_ERR;
	} else {
		udt_ev = UDT_EPOLL_IN | UDT_EPOLL_OUT | UDT_EPOLL_ERR;
	}

	rc = UDT::epoll_add_usock(eid, u, &udt_ev);
	if (rc == UDT::ERROR) {
		// error happen
		return -1;
	} else {
		// success
		return 0;
	}
}

int udt_epoll_add_ssock(int eid, SYSSOCKET s, const int * events)
{
    int rc;
    int flag = 0;

#ifdef LINUX
    if (events) {
		if (*events & UDT_UDT_EPOLL_IN)  flag |= UDT_EPOLL_IN;
		if (*events & UDT_UDT_EPOLL_OUT) flag |= UDT_EPOLL_OUT;
		if (*events & UDT_UDT_EPOLL_ERR) flag |= UDT_EPOLL_ERR;
	} else {
        flag = UDT_EPOLL_IN | UDT_EPOLL_OUT | UDT_EPOLL_ERR;
    }
#else
    flag = UDT_EPOLL_IN | UDT_EPOLL_OUT | UDT_EPOLL_ERR;
#endif

    // call UDT native function
    rc = UDT::epoll_add_ssock(eid, s, &flag);
    if (rc == UDT::ERROR) {
        // error happen
        return -1;
    } else {
        // success
        return 0;
    }
}

int udt_epoll_remove_usock(int eid, UDTSOCKET u)
{
    int rc;

    // call UDT native function
    rc = UDT::epoll_remove_usock(eid, u);
    if (rc == UDT::ERROR) {
        // error happen
        return -1;
    } else {
        // success
        return 0;
    }
}

int udt_epoll_remove_ssock(int eid, SYSSOCKET s)
{
    int rc;

    // call UDT native function
    rc = UDT::epoll_remove_ssock(eid, s);
    if (rc == UDT::ERROR) {
        // error happen
        return -1;
    } else {
        // success
        return 0;
    }
}

int udt_epoll_wait2(
		int eid,
		UDTSOCKET* readfds, int* rnum, UDTSOCKET* writefds, int* wnum,
		int64_t msTimeOut,
        SYSSOCKET* lrfds, int* lrnum, SYSSOCKET* lwfds, int* lwnum)
{
    int rc;

    // call UDT native function
    rc = UDT::epoll_wait2(
    		eid,
    		readfds, rnum, writefds, wnum,
    		msTimeOut,
    		lrfds, lrnum, lwfds, lwnum);
    if (rc == UDT::ERROR) {
        // error happen
        return -1;
    } else {
        // success
        return 0;
    }
}

int udt_epoll_release(int eid)
{
    int rc;

    // call UDT native function
    rc = UDT::epoll_release(eid);
    if (rc == UDT::ERROR) {
        // error happen
        return -1;
    } else {
        // success
        return 0;
    }
}

// constant definitions
const UDTSOCKET UDT_INVALID_SOCK = -1;
const int UDT_ERROR = -1;

}
