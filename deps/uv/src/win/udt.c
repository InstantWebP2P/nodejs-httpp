// Copyright tom zhou<iwebpp@gmail.com>, 2012.

#include <assert.h>

#include "uv.h"
#include "internal.h"
#include "handle-inl.h"
#include "stream-inl.h"
#include "req-inl.h"
#include "udtc.h" // udt head file

///#define UDT_DEBUG 1

/*
 * Threshold of active udt streams for which to preallocate udt read buffers.
 * (Due to node slab allocator performing poorly under this pattern,
 *  the optimization is temporarily disabled (threshold=0).  This will be
 *  revisited once node allocator is improved.)
 */
static const unsigned int uv_active_udt_streams_threshold = 0;

/*
 * Number of simultaneous pending AcceptEx calls.
 */
static const unsigned int uv_udt_simultaneous_server_accepts = 1;

/* A zero-size buffer for use by uv_tcp_read */
///static char uv_zero_[] = "";


static int udt__nonblock(int udtfd, int set)
{
    int block = (set ? 0 : 1);
    int rc1, rc2;

    rc1 = udt_setsockopt(udtfd, 0, (int)UDT_UDT_SNDSYN, (void *)&block, sizeof(block));
    rc2 = udt_setsockopt(udtfd, 0, (int)UDT_UDT_RCVSYN, (void *)&block, sizeof(block));

    return (rc1 | rc2);
}


static int uv__udt_nodelay(uv_udt_t* handle, SOCKET socket, int enable) {
  return 0;
}


static int uv__udt_keepalive(uv_udt_t* handle, SOCKET socket, int enable, unsigned int delay) {
	return 0;
}


static int uv_udt_set_socket(uv_loop_t* loop, uv_udt_t* handle,
    SOCKET socket, int family, int imported) {
  int non_ifs_lsp;

  assert(handle->socket == INVALID_SOCKET);

  /* Set the socket to nonblocking mode */
  if (udt__nonblock(handle->udtfd, 1) < 0) {
    uv__set_sys_error(loop, uv_translate_udt_error());
    return -1;
  }

  /* Associate it with the I/O completion port. */
  /* Use uv_handle_t pointer as completion key. */
  if (CreateIoCompletionPort((HANDLE)socket,
                             loop->iocp,
                             (ULONG_PTR)socket,
                             0) == NULL) {
      uv__set_sys_error(loop, GetLastError());
      return -1;
  }

  if (family == AF_INET6) {
    non_ifs_lsp = uv_tcp_non_ifs_lsp_ipv6;
  } else {
    non_ifs_lsp = uv_tcp_non_ifs_lsp_ipv4;
  }

  if (pSetFileCompletionNotificationModes && !non_ifs_lsp) {
    if (pSetFileCompletionNotificationModes((HANDLE) socket,
        FILE_SKIP_SET_EVENT_ON_HANDLE |
        FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)) {
      handle->flags |= UV_HANDLE_SYNC_BYPASS_IOCP;
    } else if (GetLastError() != ERROR_INVALID_FUNCTION) {
      uv__set_sys_error(loop, GetLastError());
      return -1;
    }
  }

  if ((handle->flags & UV_HANDLE_TCP_NODELAY) &&
      uv__udt_nodelay(handle, socket, 1)) {
    return -1;
  }

  /* TODO: Use stored delay. */
  if ((handle->flags & UV_HANDLE_TCP_KEEPALIVE) &&
      uv__udt_keepalive(handle, socket, 1, 60)) {
    return -1;
  }

  handle->socket = socket;

  if (family == AF_INET6) {
    handle->flags |= UV_HANDLE_IPV6;
  } else {
    assert(!(handle->flags & UV_HANDLE_IPV6));
  }

  return 0;
}


int uv_udt_init(uv_loop_t* loop, uv_udt_t* handle) {
  static int _initialized = 0;
  int i;

  // insure startup UDT
  if (_initialized == 0) {
    assert(udt_startup() == 0);
    _initialized = 1;
  }

  uv_stream_init(loop, (uv_stream_t*) handle, UV_UDT);

  handle->accept_reqs = NULL;
  handle->pending_accepts = NULL;
  handle->socket = INVALID_SOCKET;
  handle->reqs_pending = 0;
  handle->func_acceptex = NULL;
  handle->func_connectex = NULL;
  handle->processed_accepts = 0;
  handle->udtfd = -1;
  handle->udtflag = 0;

  loop->counters.udt_init++;

  // initialize dedicated requests: poll,read,write,accept,connect
  // poll
  uv_req_init(handle->loop, &handle->udtreq_poll);
  handle->udtreq_poll.type = UV_UDT_POLL;
  handle->udtreq_poll.data = handle;
  handle->udtreq_poll.udtdummy = 0;
  handle->udtreq_poll.udtflag = UV_UDT_REQ_POLL;
  // read
  uv_req_init(handle->loop, &handle->udtreq_read);
  handle->udtreq_read.type = UV_UDT_POLL;
  handle->udtreq_read.data = handle;
  handle->udtreq_read.udtdummy = 0;
  handle->udtreq_read.udtflag = UV_UDT_REQ_READ;
  // write
  uv_req_init(handle->loop, &handle->udtreq_write);
  handle->udtreq_write.type = UV_UDT_POLL;
  handle->udtreq_write.data = handle;
  handle->udtreq_write.udtdummy = 0;
  handle->udtreq_write.udtflag = UV_UDT_REQ_WRITE;
  // accept
  uv_req_init(handle->loop, &handle->udtreq_accept);
  handle->udtreq_accept.type = UV_UDT_POLL;
  handle->udtreq_accept.data = handle;
  handle->udtreq_accept.udtdummy = 0;
  handle->udtreq_accept.udtflag = UV_UDT_REQ_ACCEPT;
  // connect
  uv_req_init(handle->loop, &handle->udtreq_connect);
  handle->udtreq_connect.type = UV_UDT_POLL;
  handle->udtreq_connect.data = handle;
  handle->udtreq_connect.udtdummy = 0;
  handle->udtreq_connect.udtflag = UV_UDT_REQ_CONNECT;
  // poll error
  uv_req_init(handle->loop, &handle->udtreq_poll_error);
  handle->udtreq_poll_error.type = UV_UDT_POLL;
  handle->udtreq_poll_error.data = handle;
  handle->udtreq_poll_error.udtdummy = 0;
  handle->udtreq_poll_error.udtflag = UV_UDT_REQ_POLL_ERROR;

  // write/connect/accept queue
  handle->pending_reqs_tail_udtwrite = NULL;
  handle->pending_reqs_tail_udtconnect = NULL;
  handle->pending_reqs_tail_udtaccept = NULL;

  // enable poll active
  handle->udtflag |= UV_UDT_REQ_POLL_ACTIVE;
  INCREASE_ACTIVE_COUNT(loop, handle);

  return 0;
}


void uv_udt_endgame(uv_loop_t* loop, uv_udt_t* handle) {
  int status;
  int sys_error;
  unsigned int i;
  uv_tcp_accept_t* req;
  char dummy;


#ifdef UDT_DEBUG
  printf("%s.%d,"
		 " reqs_pending:%d,"
		 " activecnt:%d,"
		 " write_pending:%d,"
		 " shutdown_req:%d\n",
		 __FUNCTION__, __LINE__,
		 handle->reqs_pending,
		 handle->activecnt,
		 handle->write_reqs_pending,
		 handle->shutdown_req);
#endif

  if ((handle->flags & UV_HANDLE_CONNECTION) &&
      (handle->shutdown_req != NULL) &&
      (handle->write_reqs_pending == 0)) {
    UNREGISTER_HANDLE_REQ(loop, handle, handle->shutdown_req);

    if (handle->flags & UV_HANDLE_CLOSING) {
      status = -1;
      sys_error = WSAEINTR;
    } else if (udt_close(handle->udtfd) == 0) {
      // !!! udt_close always plays gracefully.
      status = 0;
      handle->flags |= UV_HANDLE_SHUT;
    } else {
      status = -1;
      sys_error = uv_translate_udt_error();
    }

    if (handle->shutdown_req->cb) {
      if (status == -1) {
        uv__set_sys_error(loop, sys_error);
      }
      handle->shutdown_req->cb(handle->shutdown_req, status);
    }

    handle->shutdown_req = NULL;
    DECREASE_PENDING_REQ_COUNT(handle);
    return;
  }

#ifdef UDT_DEBUG
  printf("%s.%d,"
		  "osfd:%d, udtsocket:%d,"
		  " reqs_pending:%d,"
		  " activecnt:%d,"
		  " write_pending:%d,"
		  " shutdown_req:%d\n",
		  __FUNCTION__, __LINE__,
		  handle->socket, handle->udtfd,
		  handle->reqs_pending,
		  handle->activecnt,
		  handle->write_reqs_pending,
		  handle->shutdown_req);
  #endif

  if ((handle->flags & UV_HANDLE_CLOSING) &&
      (handle->reqs_pending == 0)) {
    assert(!(handle->flags & UV_HANDLE_CLOSED));
    uv__handle_stop(handle);

    if (!(handle->flags & UV_HANDLE_TCP_SOCKET_CLOSED)) {
      udt_close(handle->udtfd);
      handle->flags |= UV_HANDLE_TCP_SOCKET_CLOSED;
    }

    if (!(handle->flags & UV_HANDLE_CONNECTION) && handle->accept_reqs) {
      free(handle->accept_reqs);
      handle->accept_reqs = NULL;
    }

    // close Osfd socket
    ///printf("shutdown,%s.%d\n",  __FUNCTION__, __LINE__);
    if (handle->socket != INVALID_SOCKET) {
    	while (recv(handle->socket, &dummy, sizeof(dummy), 0) > 0);
    	closesocket(handle->socket);
    	handle->socket = INVALID_SOCKET;
    }

    uv__handle_close(handle);
    loop->active_udt_streams--;
  }
}


static int uv__bind(uv_udt_t* handle,
                    int family,
                    struct sockaddr* addr,
                    int addrsize,
                    int reuseaddr,
                    int reuseable) {
  DWORD err;
  int r;
  SOCKET sock;
  int optlen, optval;

  if (handle->socket == INVALID_SOCKET) {
    handle->udtfd = udt_socket(family, SOCK_STREAM, 0);
    if (handle->udtfd < 0) {
      uv__set_sys_error(handle->loop, uv_translate_udt_error());
      return -1;
    }

	// fill Osfd
	assert(udt_getsockopt(handle->udtfd, 0, (int)UDT_UDT_OSFD, &sock, &optlen) == 0);

	if (uv_udt_set_socket(handle->loop, handle, sock, family, 0) == -1) {
       closesocket(sock);
       udt_close(handle->udtfd);
       return -1;
    }

    // TBD... optimization on mobile device
    /* Set UDT congestion control algorithms */
    if (udt_setccc(handle->udtfd, UDT_CCC_UDT) < 0) {
       closesocket(sock);
       udt_close(handle->udtfd);
       return -1;
    }

    /* Set UDT buffer size */
    // optimization for node.js
    // - set maxWindowSize from 25600 to 2560, UDT/UDP buffer from 10M/1M to 1M/100K
    // - ??? or            from 25600 to 5120, UDT/UDP buffer from 10M/1M to 2M/200K
    optval = 5120;
    if (udt_setsockopt(handle->udtfd, 0, (int)UDT_UDT_FC, (void *)&optval, sizeof(optval))) {
       closesocket(sock);
       udt_close(handle->udtfd);
       return -1;
    }
    optval = 204800;
    if (udt_setsockopt(handle->udtfd, 0, (int)UDT_UDP_SNDBUF, (void *)&optval, sizeof(optval)) |
    	udt_setsockopt(handle->udtfd, 0, (int)UDT_UDP_RCVBUF, (void *)&optval, sizeof(optval))) {
       closesocket(sock);
       udt_close(handle->udtfd);
       return -1;
    }
    optval = 2048000;
    if (udt_setsockopt(handle->udtfd, 0, (int)UDT_UDT_SNDBUF, (void *)&optval, sizeof(optval)) |
    	udt_setsockopt(handle->udtfd, 0, (int)UDT_UDT_RCVBUF, (void *)&optval, sizeof(optval))) {
       closesocket(sock);
       udt_close(handle->udtfd);
       return -1;
    }
    ///////////////////////////////////////////////////////////////////////////////////////////////

    // check if REUSE ADDR ///////////
    if (reuseaddr >= 0)
    {
        udt_setsockopt(handle->udtfd, 0, (int)UDT_UDT_REUSEADDR, &reuseaddr, sizeof reuseaddr);
    }
    // check if allow REUSE ADDR
    if (reuseable >= 0)
    {
        udt_setsockopt(handle->udtfd, 0, (int)UDT_UDT_REUSEABLE, &reuseable, sizeof reuseable);
    }
    ///////////////////////////////////
  }

  r = udt_bind(handle->udtfd, addr, addrsize);
  // fill UDP FD
  //assert(udt_getsockopt(handle->udtfd, 0, (int)UDT_UDT_UDPFD, &handle->udpfd, &optlen) == 0);

  if (r < 0) {
    err = uv_translate_udt_error();
    if (err == WSAEADDRINUSE) {
      /* Some errors are not to be reported until connect() or listen() */
      handle->bind_error = err;
      handle->flags |= UV_HANDLE_BIND_ERROR;
    } else {
      uv__set_sys_error(handle->loop, err);
      return -1;
    }
  }

  handle->flags |= UV_HANDLE_BOUND;

  return 0;
}


int uv__udt_bind(uv_udt_t* handle, struct sockaddr_in addr, int reuseaddr, int reuseable) {
  return uv__bind(handle,
                  AF_INET,
                  (struct sockaddr*)&addr,
                  sizeof(struct sockaddr_in),
                  reuseaddr, reuseable);
}


int uv__udt_bind6(uv_udt_t* handle, struct sockaddr_in6 addr, int reuseaddr, int reuseable) {
  return uv__bind(handle,
                  AF_INET6,
                  (struct sockaddr*)&addr,
                  sizeof(struct sockaddr_in6),
                  reuseaddr, reuseable);
}


static int uv__bindfd(
    	uv_udt_t* handle,
        SOCKET udpfd,
        int reuseaddr,
        int reuseable) {
  DWORD err;
  int r;
  SOCKET sock;
  int optlen, optval;
  struct sockaddr_storage addr;
  socklen_t addrlen = sizeof(addr);
  int family = AF_INET;

  if (handle->socket == INVALID_SOCKET) {
    // extract domain info by existing udpfd ///////////////////////////////
    if (getsockname(udpfd, (struct sockaddr *)&addr, &addrlen) < 0) {
		uv__set_sys_error(handle->loop, WSAGetLastError());
	    return -1;
    }
    family = addr.ss_family;
    ////////////////////////////////////////////////////////////////////////

    handle->udtfd = udt_socket(family, SOCK_STREAM, 0);
    if (handle->udtfd < 0) {
      uv__set_sys_error(handle->loop, uv_translate_udt_error());
      return -1;
    }
	
    // fill Osfd
    assert(udt_getsockopt(handle->udtfd, 0, (int)UDT_UDT_OSFD, &sock, &optlen) == 0);

	if (uv_udt_set_socket(handle->loop, handle, sock, family, 0) == -1) {
      closesocket(sock);
      udt_close(handle->udtfd);
      return -1;
    }

    // TBD... optimization on mobile device
    /* Set UDT congestion control algorithms */
    if (udt_setccc(handle->udtfd, UDT_CCC_UDT) < 0) {
      closesocket(sock);
      udt_close(handle->udtfd);
      return -1;
    }

    /* Set UDT buffer size */
    // optimization for node.js
    // - set maxWindowSize from 25600 to 2560, UDT/UDP buffer from 10M/1M to 1M/100K
    // - ??? or            from 25600 to 5120, UDT/UDP buffer from 10M/1M to 2M/200K
    optval = 5120;
    if (udt_setsockopt(handle->udtfd, 0, (int)UDT_UDT_FC, (void *)&optval, sizeof(optval))) {
       closesocket(sock);
       udt_close(handle->udtfd);
       return -1;
    }
    optval = 204800;
    if (udt_setsockopt(handle->udtfd, 0, (int)UDT_UDP_SNDBUF, (void *)&optval, sizeof(optval)) |
    	udt_setsockopt(handle->udtfd, 0, (int)UDT_UDP_RCVBUF, (void *)&optval, sizeof(optval))) {
       closesocket(sock);
       udt_close(handle->udtfd);
       return -1;
    }
    optval = 2048000;
    if (udt_setsockopt(handle->udtfd, 0, (int)UDT_UDT_SNDBUF, (void *)&optval, sizeof(optval)) |
    	udt_setsockopt(handle->udtfd, 0, (int)UDT_UDT_RCVBUF, (void *)&optval, sizeof(optval))) {
       closesocket(sock);
       udt_close(handle->udtfd);
       return -1;
    }
    ///////////////////////////////////////////////////////////////////////////////////////////////

    // check if REUSE ADDR ///////////
    if (reuseaddr >= 0)
    {
        udt_setsockopt(handle->udtfd, 0, (int)UDT_UDT_REUSEADDR, &reuseaddr, sizeof reuseaddr);
    }
    // check if allow REUSE ADDR
    if (reuseable >= 0)
    {
        udt_setsockopt(handle->udtfd, 0, (int)UDT_UDT_REUSEABLE, &reuseable, sizeof reuseable);
    }
    ///////////////////////////////////
  }

  r = udt_bind2(handle->udtfd, udpfd); 
  // fill UDP FD
  //assert(udt_getsockopt(handle->udtfd, 0, (int)UDT_UDT_UDPFD, &handle->udpfd, &optlen) == 0);
  //assert(udpfd == handle->udpfd);

  if (r < 0) {
    err = uv_translate_udt_error();
    if (err == WSAEADDRINUSE) {
      /* Some errors are not to be reported until connect() or listen() */
      handle->bind_error = err;
      handle->flags |= UV_HANDLE_BIND_ERROR;
    } else {
      uv__set_sys_error(handle->loop, err);
      return -1;
    }
  }

  handle->flags |= UV_HANDLE_BOUND;

  return 0;
}

int uv__udt_bindfd(uv_udt_t* handle, uv_os_sock_t udpfd) {
    return uv__bindfd(handle, udpfd);
}


// Methods to insert pending accept/connect/write/read request
static void udt_insert_pending_req_udtwrite(uv_loop_t* loop, uv_udt_t* handle, uv_req_t* req) {
  req->next_req = NULL;
  if (handle->pending_reqs_tail_udtwrite) {
    req->next_req = handle->pending_reqs_tail_udtwrite->next_req;
    handle->pending_reqs_tail_udtwrite->next_req = req;
    handle->pending_reqs_tail_udtwrite = req;
  } else {
    req->next_req = req;
    handle->pending_reqs_tail_udtwrite = req;
  }
}

static void udt_insert_pending_req_udtconnect(uv_loop_t* loop, uv_udt_t* handle, uv_req_t* req) {
  req->next_req = NULL;
  if (handle->pending_reqs_tail_udtconnect) {
    req->next_req = handle->pending_reqs_tail_udtconnect->next_req;
    handle->pending_reqs_tail_udtconnect->next_req = req;
    handle->pending_reqs_tail_udtconnect = req;
  } else {
    req->next_req = req;
    handle->pending_reqs_tail_udtconnect = req;
  }
}

static void udt_insert_pending_req_udtaccept(uv_loop_t* loop, uv_udt_t* handle, uv_req_t* req) {
  req->next_req = NULL;
  if (handle->pending_reqs_tail_udtaccept) {
    req->next_req = handle->pending_reqs_tail_udtaccept->next_req;
    handle->pending_reqs_tail_udtaccept->next_req = req;
    handle->pending_reqs_tail_udtaccept = req;
  } else {
    req->next_req = req;
    handle->pending_reqs_tail_udtaccept = req;
  }
}

// Enqueue dummy poll request to capture udt event
static void uv_udt_queue_poll(uv_loop_t* loop, uv_udt_t* handle) {
	uv_req_t* req;
	uv_buf_t buf;
	DWORD result, bytes, flags;
	unsigned long nread = 0;


    if (handle->udtflag & (UV_UDT_REQ_POLL | UV_UDT_REQ_POLL_ERROR))
    	return;

	/*
	 * Preallocate a read buffer of zero-byte
	 */
	req = &handle->udtreq_poll;
	buf.base = (char*) &(req->udtdummy);
	buf.len = 0;

	/* Prepare the overlapped structure. */
	memset(&(req->overlapped), 0, sizeof(req->overlapped));

	/* Test if there is a pending event */
	assert(ioctlsocket(handle->socket, FIONREAD, &nread) == 0);

	/* Launch recv request */
	if (nread == 0) {
		flags = 0;
		result = WSARecv(
				handle->socket,
				(WSABUF*)&buf,
				1,
				&bytes,
				&flags,
				&req->overlapped,
				NULL);
	}

#ifdef UDT_DEBUG
	printf("%s:%d, %s, osfd:%d, udtsocket:%d, WSARecv bytes:%d, result:%d, errcode:%d\n",
			__FUNCTION__, __LINE__,
			(handle->flags & UV_HANDLE_LISTENING) ? "server" : "client",
			handle->socket, handle->udtfd, bytes, result, WSAGetLastError());
#endif

	if (nread > 0) {
		uv_insert_pending_req(loop, req);
		handle->reqs_pending++;
		handle->udtflag |= UV_UDT_REQ_POLL;
	} else if ((result == SOCKET_ERROR ) &&
			   (WSAGetLastError() == WSA_IO_PENDING)) {
		handle->reqs_pending++;
		handle->udtflag |= UV_UDT_REQ_POLL;
	} else {
		/* Make this req pending reporting an error. */
		req = &handle->udtreq_poll_error;

		SET_REQ_ERROR(req, WSAGetLastError());
		uv_insert_pending_req(loop, req);
		handle->reqs_pending++;
		handle->udtflag |= UV_UDT_REQ_POLL_ERROR;
	}

	return;
}


static void uv_udt_queue_accept(uv_udt_t* handle, uv_tcp_accept_t* req) {
  uv_loop_t* loop = handle->loop;
  BOOL success;
  DWORD bytes;
  short family;
  uv_buf_t buf;
  DWORD flags;


  assert(handle->flags & UV_HANDLE_LISTENING);
  assert(req->accept_socket == INVALID_SOCKET);

  /* choose family and extension function */
  if (handle->flags & UV_HANDLE_IPV6) {
    family = AF_INET6;
  } else {
    family = AF_INET;
  }

  /* Prepare the overlapped structure. */
  memset(&(req->overlapped), 0, sizeof(req->overlapped));

  // 1.
  // queue poll request
  uv_udt_queue_poll(loop, handle);

  // 2.
  // enqueue accept request
  ///handle->reqs_pending++;
  ///printf("%s:%d, reqs_pending:%d\n", __FUNCTION__, __LINE__, handle->reqs_pending);
  udt_insert_pending_req_udtaccept(loop, handle, (uv_req_t*)req);
}


static void uv_udt_queue_read(uv_loop_t* loop, uv_udt_t* handle) {
  uv_read_t* req;
  ///uv_buf_t buf;
  int result;
  DWORD bytes, flags;


  assert(handle->flags & UV_HANDLE_READING);
  assert(!(handle->flags & UV_HANDLE_READ_PENDING));

  req = &handle->read_req;

  /*
   * Preallocate a read buffer if the number of active streams is below
   * the threshold.
  */
  if (loop->active_udt_streams < uv_active_udt_streams_threshold) {
	// never go here, tom tom
	assert(0);

    handle->flags &= ~UV_HANDLE_ZERO_READ;
    ///handle->read_buffer = handle->alloc_cb((uv_handle_t*) handle, 65536);
    ///assert(handle->read_buffer.len > 0);
    ///buf = handle->read_buffer;
  } else {
    handle->flags |= UV_HANDLE_ZERO_READ;
    ///buf.base = (char*)uv_zero_;
    ///buf.len = 0;
  }

  /* Prepare the overlapped structure. */
  memset(&(req->overlapped), 0, sizeof(req->overlapped));

  // 1.
  // queue poll request
  uv_udt_queue_poll(loop, handle);

  // 2.
  // enqueue read request
  handle->flags |= UV_HANDLE_READ_PENDING;
  ///handle->reqs_pending++;
  ///printf("%s:%d, reqs_pending:%d\n", __FUNCTION__, __LINE__, handle->reqs_pending);
}


int uv_udt_listen(uv_udt_t* handle, int backlog, uv_connection_cb cb) {
  uv_loop_t* loop = handle->loop;
  unsigned int i, simultaneous_accepts;
  uv_tcp_accept_t* req;

  assert(backlog > 0);

  if (handle->flags & UV_HANDLE_LISTENING) {
    handle->connection_cb = cb;
  }

  if (handle->flags & UV_HANDLE_READING) {
    uv__set_artificial_error(loop, UV_EISCONN);
    return -1;
  }

  if (handle->flags & UV_HANDLE_BIND_ERROR) {
    uv__set_sys_error(loop, handle->bind_error);
    return -1;
  }

  if (!(handle->flags & UV_HANDLE_BOUND) &&
      uv_udt_bind(handle, uv_addr_ip4_any_) < 0)
    return -1;

  if (!(handle->flags & UV_HANDLE_SHARED_TCP_SOCKET) &&
      (udt_listen(handle->udtfd, backlog)) < 0) {
    uv__set_sys_error(loop, uv_translate_udt_error());
    return -1;
  }

  handle->flags |= UV_HANDLE_LISTENING;
  handle->connection_cb = cb;
  INCREASE_ACTIVE_COUNT(loop, handle);

  simultaneous_accepts = handle->flags & UV_HANDLE_TCP_SINGLE_ACCEPT ? 1
    : uv_udt_simultaneous_server_accepts;

  if (!handle->accept_reqs) {
    handle->accept_reqs = (uv_tcp_accept_t*)
      malloc(uv_udt_simultaneous_server_accepts * sizeof(uv_tcp_accept_t));
    if (!handle->accept_reqs) {
      uv_fatal_error(ERROR_OUTOFMEMORY, "malloc");
    }

    for (i = 0; i < simultaneous_accepts; i++) {
      req = &handle->accept_reqs[i];
      uv_req_init(loop, (uv_req_t*)req);
      req->type = UV_ACCEPT;
      req->accept_socket = INVALID_SOCKET;
      req->data = handle;

      req->wait_handle = INVALID_HANDLE_VALUE;
      if (handle->flags & UV_HANDLE_EMULATE_IOCP) {
        req->event_handle = CreateEvent(NULL, 0, 0, NULL);
        if (!req->event_handle) {
          uv_fatal_error(GetLastError(), "CreateEvent");
        }
      } else {
        req->event_handle = NULL;
      }

      uv_udt_queue_accept(handle, req);
    }

    /* Initialize other unused requests too, because uv_udt_endgame */
    /* doesn't know how how many requests were intialized, so it will */
    /* try to clean up {uv_udt_simultaneous_server_accepts} requests. */
    for (i = simultaneous_accepts; i < uv_udt_simultaneous_server_accepts; i++) {
      req = &handle->accept_reqs[i];
      uv_req_init(loop, (uv_req_t*) req);
      req->type = UV_ACCEPT;
      req->accept_socket = INVALID_SOCKET;
      req->data = handle;
      req->wait_handle = INVALID_HANDLE_VALUE;
    }
  }

  return 0;
}


int uv_udt_accept(uv_udt_t* server, uv_udt_t* client) {
  uv_loop_t* loop = server->loop;
  int rv = 0;
  int family;
  struct sockaddr_storage saddr;
  int namelen = sizeof saddr;
  int optlen;
  ///char clienthost[NI_MAXHOST];
  ///char clientservice[NI_MAXSERV];
  uv_tcp_accept_t* req = server->pending_accepts;
  uv_buf_t buf;
  DWORD flags, bytes;
  BOOL success;
  

  if (!req) {
    /* No valid connections found, so we error out. */
    uv__set_sys_error(loop, WSAEWOULDBLOCK);
    return -1;
  }

  // call udt accept
  ///////////////////////////////////////////////////////////////////////////////////////////
  req->accept_udtfd = udt_accept(server->udtfd, (struct sockaddr *)&saddr, &namelen);
  if (req->accept_udtfd < 0) {
	  if ((udt_getlasterror_code() == UDT_EASYNCRCV) ||
		  (udt_getlasterror_code() == UDT_ESECFAIL)) {
		  uv__set_sys_error(loop, WSAEWOULDBLOCK);
		  req->accept_socket = INVALID_SOCKET;

		  // queue poll request
		  uv_udt_queue_poll(loop, server);

		  return 0;
	  } else {
		  uv__set_sys_error(loop, WSAENOTCONN);
		  req->accept_socket = INVALID_SOCKET;
		  return -1;
	  }
  }
  client->udtfd = req->accept_udtfd;
  // fill Os fd, udp fd
  assert(udt_getsockopt(client->udtfd, 0, (int)UDT_UDT_OSFD, &req->accept_socket, &optlen) == 0);
  //assert(udt_getsockopt(client->udtfd, 0, (int)UDT_UDT_UDPFD, &req->accept_udt_udpfd, &optlen) == 0);
  //client->udpfd = req->accept_udt_udpfd;

  if (server->flags & UV_HANDLE_IPV6) {
    family = AF_INET6;
  } else {
    family = AF_INET;
  }

  if (uv_udt_set_socket(client->loop, client, req->accept_socket, family, 0) == -1) {
	  closesocket(req->accept_socket);
	  udt_close(client->udtfd);
	  rv = -1;
  } else {
	  uv_connection_init((uv_stream_t*) client);
	  client->flags |= UV_HANDLE_BOUND;
	  
	  ///getnameinfo((struct sockaddr*)&saddr, sizeof saddr, clienthost, sizeof(clienthost), clientservice, sizeof(clientservice), NI_NUMERICHOST|NI_NUMERICSERV);
	  ///printf("new connection: %s:%s\n", clienthost, clientservice);
  }
  //////////////////////////////////////////////////////////////////////////////////////////////////

  /* Prepare the req to pick up a new connection */
  server->pending_accepts = req->next_pending;
  req->next_pending = NULL;
  req->accept_socket = INVALID_SOCKET;

  if (!(server->flags & UV_HANDLE_CLOSING)) {
    /* Check if we're in a middle of changing the number of pending accepts. */
    if (!(server->flags & UV_HANDLE_TCP_ACCEPT_STATE_CHANGING)) {
      uv_udt_queue_accept(server, req);
    } else {
      /* We better be switching to a single pending accept. */
      assert(server->flags & UV_HANDLE_TCP_SINGLE_ACCEPT);

      server->processed_accepts++;

      if (server->processed_accepts >= uv_udt_simultaneous_server_accepts) {
        server->processed_accepts = 0;
        /*
         * All previously queued accept requests are now processed.
         * We now switch to queueing just a single accept.
         */
        uv_udt_queue_accept(server, &server->accept_reqs[0]);
        server->flags &= ~UV_HANDLE_TCP_ACCEPT_STATE_CHANGING;
        server->flags |= UV_HANDLE_TCP_SINGLE_ACCEPT;
      }
    }
  }

  loop->active_udt_streams++;

  return rv;
}


int uv_udt_read_start(uv_udt_t* handle, uv_alloc_cb alloc_cb,
    uv_read_cb read_cb) {
  uv_loop_t* loop = handle->loop;

  if (!(handle->flags & UV_HANDLE_CONNECTION)) {
    uv__set_sys_error(loop, WSAEINVAL);
    return -1;
  }

  if (handle->flags & UV_HANDLE_READING) {
    uv__set_sys_error(loop, WSAEALREADY);
    return -1;
  }
  
  if (handle->flags & UV_HANDLE_EOF) {
    uv__set_sys_error(loop, WSAESHUTDOWN);
    return -1;
  }

  handle->flags |= UV_HANDLE_READING;
  handle->read_cb = read_cb;
  handle->alloc_cb = alloc_cb;
  INCREASE_ACTIVE_COUNT(loop, handle);

  /* If reading was stopped and then started again, there could still be a */
  /* read request pending. */
  if (!(handle->flags & UV_HANDLE_READ_PENDING)) {
    uv_udt_queue_read(loop, handle);
  }

  return 0;
}


int uv__udt_connect(uv_connect_t* req,
                    uv_udt_t* handle,
                    struct sockaddr_in address,
                    uv_connect_cb cb) {
  uv_loop_t* loop = handle->loop;
  int addrsize = sizeof(struct sockaddr_in);
  BOOL success;
  DWORD bytes, flags;
  uv_buf_t buf;
  int rc, stats;


  if (handle->flags & UV_HANDLE_BIND_ERROR) {
    uv__set_sys_error(loop, handle->bind_error);
    return -1;
  }

  if (!(handle->flags & UV_HANDLE_BOUND) &&
      uv_udt_bind(handle, uv_addr_ip4_any_) < 0)
    return -1;

  uv_req_init(loop, (uv_req_t*) req);
  req->type = UV_CONNECT;
  req->handle = (uv_stream_t*) handle;
  req->cb = cb;
  memset(&req->overlapped, 0, sizeof(req->overlapped));

  // call udt connect
  rc = udt_connect(handle->udtfd, (struct sockaddr*)&address, addrsize);

  stats = udt_getsockstate(handle->udtfd);
  if (UDT_CONNECTED == stats) {
	  // insert request immediately
	  ///handle->reqs_pending++;
	  ///printf("%s:%d, reqs_pending:%d\n", __FUNCTION__, __LINE__, handle->reqs_pending);
	  ///REGISTER_HANDLE_REQ(loop, handle, req);
	  udt_insert_pending_req_udtconnect(loop, handle, (uv_req_t*)req);

	  // insert poll request
	  if (!(handle->udtflag & UV_UDT_REQ_CONNECT)) {
		  handle->udtflag |= UV_UDT_REQ_CONNECT;

		  uv_insert_pending_req(loop, &handle->udtreq_connect);
		  ///handle->reqs_pending++;
		  ///printf("%s:%d, reqs_pending:%d\n", __FUNCTION__, __LINE__, handle->reqs_pending);
	  }
  } else if (UDT_CONNECTING == stats) {
	  // 1.
	  // queue poll request
	  uv_udt_queue_poll(loop, handle);

	  // 2.
	  // enqueue connect request
	  ///handle->reqs_pending++;
	  ///printf("%s:%d, reqs_pending:%d\n", __FUNCTION__, __LINE__, handle->reqs_pending);
	  ///REGISTER_HANDLE_REQ(loop, handle, req);
	  udt_insert_pending_req_udtconnect(loop, handle, (uv_req_t*)req);
  } else {
	  uv__set_sys_error(loop, uv_translate_udt_error());
	  return -1;
  }

  return 0;
}


int uv__udt_connect6(uv_connect_t* req,
                     uv_udt_t* handle,
                     struct sockaddr_in6 address,
                     uv_connect_cb cb) {
  uv_loop_t* loop = handle->loop;
  int addrsize = sizeof(struct sockaddr_in6);
  BOOL success;
  DWORD bytes, flags;
  uv_buf_t buf;
  int rc, stats;


  if (handle->flags & UV_HANDLE_BIND_ERROR) {
    uv__set_sys_error(loop, handle->bind_error);
    return -1;
  }

  if (!(handle->flags & UV_HANDLE_BOUND) &&
      uv_udt_bind6(handle, uv_addr_ip6_any_) < 0)
    return -1;

  uv_req_init(loop, (uv_req_t*) req);
  req->type = UV_CONNECT;
  req->handle = (uv_stream_t*) handle;
  req->cb = cb;
  memset(&req->overlapped, 0, sizeof(req->overlapped));

  // call udt connect
  rc = udt_connect(handle->udtfd, (struct sockaddr*)&address, addrsize);

  stats = udt_getsockstate(handle->udtfd);
  if (UDT_CONNECTED == stats) {
	  // insert request immediately
	  ///handle->reqs_pending++;
	  ///printf("%s:%d, reqs_pending:%d\n", __FUNCTION__, __LINE__, handle->reqs_pending);
	  ///REGISTER_HANDLE_REQ(loop, handle, req);
	  udt_insert_pending_req_udtconnect(loop, handle, (uv_req_t*)req);

	  // insert poll request
	  if (!(handle->udtflag & UV_UDT_REQ_CONNECT)) {
		  handle->udtflag |= UV_UDT_REQ_CONNECT;
		  uv_insert_pending_req(loop, &handle->udtreq_connect);
		  ///handle->reqs_pending++;
		  ///printf("%s:%d, reqs_pending:%d\n", __FUNCTION__, __LINE__, handle->reqs_pending);
	  }
  } else if (UDT_CONNECTING == stats) {
 	 // 1.
 	 // queue poll request
 	 uv_udt_queue_poll(loop, handle);

 	 // 2.
 	 // enqueue connect request
 	 ///handle->reqs_pending++;
 	 ///printf("%s:%d, reqs_pending:%d\n", __FUNCTION__, __LINE__, handle->reqs_pending);
 	 ///REGISTER_HANDLE_REQ(loop, handle, req);
 	 udt_insert_pending_req_udtconnect(loop, handle, (uv_req_t*)req);
  } else {
 	 uv__set_sys_error(loop, uv_translate_udt_error());
 	 return -1;
  }

  return 0;
}


int uv_udt_getsockname(uv_udt_t* handle, struct sockaddr* name,
    int* namelen) {
  uv_loop_t* loop = handle->loop;
  int result;


  if (!(handle->flags & UV_HANDLE_BOUND)) {
    uv__set_sys_error(loop, WSAEINVAL);
    return -1;
  }

  if (handle->flags & UV_HANDLE_BIND_ERROR) {
    uv__set_sys_error(loop, handle->bind_error);
    return -1;
  }

  result = udt_getsockname(handle->udtfd, name, namelen);
  if (result != 0) {
    uv__set_sys_error(loop, uv_translate_udt_error());
    return -1;
  }

  return 0;
}


int uv_udt_getpeername(uv_udt_t* handle, struct sockaddr* name,
    int* namelen) {
  uv_loop_t* loop = handle->loop;
  int result;


  if (!(handle->flags & UV_HANDLE_BOUND)) {
    uv__set_sys_error(loop, WSAEINVAL);
    return -1;
  }

  if (handle->flags & UV_HANDLE_BIND_ERROR) {
    uv__set_sys_error(loop, handle->bind_error);
    return -1;
  }

  result = udt_getpeername(handle->udtfd, name, namelen);
  if (result != 0) {
    uv__set_sys_error(loop, uv_translate_udt_error());
    return -1;
  }

  return 0;
}


// Process pending accept/connect/read/write request
INLINE static void udt_process_reqs_udtwrite(uv_loop_t* loop, uv_udt_t* handle) {
	uv_req_t* raw_req;
	uv_req_t* first;
	uv_req_t* next;
	uv_req_t* tail;
	int stop = 0;
	int werr = 0;

	if (handle->pending_reqs_tail_udtwrite == NULL) {
		return;
	}

	first = handle->pending_reqs_tail_udtwrite->next_req;
	next = first;
	tail = handle->pending_reqs_tail_udtwrite;
	handle->pending_reqs_tail_udtwrite = NULL;

	while (next != NULL) {
		raw_req = next;
		next = raw_req->next_req != first ? raw_req->next_req : NULL;

		// process write request one by one
		{
			BOOL success;
			DWORD bytes, flags;
			uv_buf_t buf;
			uv_buf_t *bufs;
			int bufcnt;
			int next, n, it, rc = 0;
			uv_write_t* req = (uv_write_t*)raw_req;

			assert(handle->write_queue_size >= req->queued_bytes);

			// continue on write
			if (req->queued_bytes > 0) {
				bufs = (uv_buf_t*) &(req->bufs[req->write_index]);
				bufcnt = req->bufcnt - req->write_index;

				// 1.
				// try write on udt until got EAGAIN or send done
				next = 1;
				n = -1;
				for (it = 0; it < bufcnt; it ++) {
					ULONG ilen = 0;

					assert(bufs[it].len < 0x80000000); // avoid buf.len out of range
					while (ilen < bufs[it].len) {
						rc = udt_send(handle->udtfd, bufs[it].base+ilen, bufs[it].len-ilen, 0);
						if (rc < 0) {
							next = 0;
							break;
						} else  {
							if (n == -1) n = 0;
							n += rc;
							ilen += rc;
						}
					}
					if (next == 0) break;
				}

#ifdef UDT_DEBUG
				printf("%s:%d, sent bytes:%d, queue bytes:%d\n",
						__FUNCTION__, __LINE__,
						n,
						req->queued_bytes );
#endif

				// 2. then queue write request with the rest of buffer
				if (n == -1) {
					// nothing to send done, queue request with iocp or check errors
					if (udt_getlasterror_code() == UDT_EASYNCSND) {
						// got EAGAIN, waiting for next event

						// queue poll request
						uv_udt_queue_poll(loop, handle);

						// skip next loop
						stop = 1;
					} else {
						uv__set_sys_error(loop, uv_translate_udt_error());

						// record error
						werr = 1;
					}
				} else {
					assert(n <= req->queued_bytes);

					if (n == req->queued_bytes) {
						// well done
						req->queued_bytes -= n;
						handle->write_queue_size -= n;
					} else {
						// partial done, trigger write request again, then check error status
						req->queued_bytes -= n;
						handle->write_queue_size -= n;

						// update bufs
						while (n > 0) {
							uv_buf_t* buf = &(req->bufs[req->write_index]);
							ULONG len = buf->len;

							assert(req->write_index < req->bufcnt);

							if ((ULONG)n < len) {
								buf->base += n;
								buf->len -= n;

								n = 0;
							} else {
								/* Finished writing the buf at index req->write_index. */
								req->write_index++;

								n -= len;
							}
						}
					}

					// check error status
					if (rc < 0) {
						if (udt_getlasterror_code() == UDT_EASYNCSND) {
							// got EAGAIN, waiting for next event

							// queue poll request
							uv_udt_queue_poll(loop, handle);

							// skip next loop
							stop = 1;
						} else {
							uv__set_sys_error(loop, uv_translate_udt_error());

							// record error
							werr = 1;
						}
					}
				}
			}

			// check results: send done, or got error
			if ((req->queued_bytes == 0) || werr) {
				// release in case allocated bufs, when error happened
				if ((req->bufcnt > UV_REQ_BUFSML_SIZE) && (req->bufs)) {
					free(req->bufs);
					req->bufs = NULL;
					req->bufcnt = 0;
				}

				// 3.
				// if write done, then callback
				///UNREGISTER_HANDLE_REQ(loop, handle, req);

				if (req->cb) {
					if (werr) uv__set_sys_error(loop, uv_translate_udt_error());
					((uv_write_cb)req->cb)(req, werr ? -1 : 0);
				}

				handle->write_reqs_pending--;
				if ((handle->flags & UV_HANDLE_SHUTTING) &&
					(handle->write_reqs_pending == 0)) {
					uv_want_endgame(loop, (uv_handle_t*)handle);
				}

				///DECREASE_PENDING_REQ_COUNT(handle);
			}

			// exit loops
			if (stop && !werr) {
				tail->next_req = req;
				handle->pending_reqs_tail_udtwrite = tail;
				break;
			}
		}
	}
}

INLINE static void udt_process_reqs_udtconnect(uv_loop_t* loop, uv_udt_t* handle) {
	uv_req_t* raw_req;
	uv_req_t* first;
	uv_req_t* next;
	uv_req_t* tail;

	if (handle->pending_reqs_tail_udtconnect == NULL) {
		return;
	}

	first = handle->pending_reqs_tail_udtconnect->next_req;
	next = first;
	tail = handle->pending_reqs_tail_udtconnect;
	handle->pending_reqs_tail_udtconnect = NULL;

	while (next != NULL) {
		raw_req = next;
		next = raw_req->next_req != first ? raw_req->next_req : NULL;

		{
			DWORD flags, bytes;
			BOOL success;
			uv_buf_t buf;
			uv_connect_t* req = (uv_connect_t*)raw_req;

			///UNREGISTER_HANDLE_REQ(loop, handle, req);

			// TBD... checking on real connect operation
			if (1/*REQ_SUCCESS(req)*/) {
				// check udt socket state
				if (UDT_CONNECTED == udt_getsockstate(handle->udtfd)) {
					uv_connection_init((uv_stream_t*)handle);
					loop->active_udt_streams++;
					((uv_connect_cb)req->cb)(req, 0);
				} else {
					uv__set_sys_error(loop, uv_translate_udt_error());
					((uv_connect_cb)req->cb)(req, -1);
				}
			} else {
				uv__set_sys_error(loop, WSAGetLastError());
				((uv_connect_cb)req->cb)(req, -1);
			}

			///DECREASE_PENDING_REQ_COUNT(handle);
		}
	}
}

INLINE static void udt_process_reqs_udtaccept(uv_loop_t* loop, uv_udt_t* handle) {
	uv_req_t* raw_req;
	uv_req_t* first;
	uv_req_t* next;
	uv_req_t* tail;

	if (handle->pending_reqs_tail_udtaccept == NULL) {
		return;
	}

	first = handle->pending_reqs_tail_udtaccept->next_req;
	next = first;
	tail = handle->pending_reqs_tail_udtaccept;
	handle->pending_reqs_tail_udtaccept = NULL;

	while (next != NULL) {
		raw_req = next;
		next = raw_req->next_req != first ? raw_req->next_req : NULL;

		// process accept request one by one
		{
			DWORD flags, bytes;
			BOOL success;
			uv_buf_t buf;
			uv_tcp_accept_t* req = (uv_tcp_accept_t*) raw_req;

			// TBD... checking on real accept operation
			if (1/*REQ_SUCCESS(req)*/) {
				req->next_pending = handle->pending_accepts;
				handle->pending_accepts = req;

				/* Accept and SO_UPDATE_ACCEPT_CONTEXT were successful. */
				if (handle->connection_cb) {
					handle->connection_cb((uv_stream_t*)handle, 0);
				}
			} else {
				/* Error related to accepted socket is ignored because the server */
				/* socket may still be healthy. If the server socket is broken
                /* uv_queue_accept will detect it. */
				req->accept_socket = INVALID_SOCKET;
				if (handle->flags & UV_HANDLE_LISTENING) {
					uv_udt_queue_accept(handle, req);
				}
			}

			///DECREASE_PENDING_REQ_COUNT(handle);
		}

	}
}

INLINE static void udt_process_reqs_udtread(uv_loop_t* loop, uv_udt_t* handle) {
	uv_read_t* req = &handle->read_req;

	// process read request once
	{
		DWORD bytes, err, flags;
		uv_buf_t buf;
		BOOL success;
		int next, rcnt;

		handle->flags &= ~UV_HANDLE_READ_PENDING;

		// TBD... check real udt error
		if (0/*!REQ_SUCCESS(req)*/) {
			/* An error occurred doing the read. */
			if ((handle->flags & UV_HANDLE_READING) ||
				!(handle->flags & UV_HANDLE_ZERO_READ)) {
				if (handle->flags & UV_HANDLE_READING) {
					handle->flags &= ~UV_HANDLE_READING;
					DECREASE_ACTIVE_COUNT(loop, handle);
				}
				handle->flags |= UV_HANDLE_EOF;

				buf = (handle->flags & UV_HANDLE_ZERO_READ) ?
						uv_buf_init(NULL, 0) : handle->read_buffer;

				err = GET_REQ_SOCK_ERROR(req);

				if (err == WSAECONNABORTED) {
					/*
					 * Turn WSAECONNABORTED into UV_ECONNRESET to be consistent with Unix.
					 */
					uv__set_error(loop, UV_ECONNRESET, err);
				} else {
					uv__set_sys_error(loop, err);
				}

				handle->read_cb((uv_stream_t*)handle, -1, buf);
			}
		} else {
			/* Do nonblocking reads until the buffer is empty */
			next = 1;
			while (handle->flags & UV_HANDLE_READING) {
				buf = handle->alloc_cb((uv_handle_t*) handle, 65536);
				assert(buf.len > 0);

				bytes = 0;
				while (bytes < buf.len) {
					rcnt = udt_recv(handle->udtfd, buf.base+bytes, buf.len-bytes, 0);;
					if (rcnt > 0) {
						bytes += rcnt;
					} else {
						err = uv_translate_udt_error();
						if (err == WSAEWOULDBLOCK) {
							/* Read buffer was completely empty, report a 0-byte read. */
							uv__set_sys_error(loop, WSAEWOULDBLOCK);
							handle->read_cb((uv_stream_t*)handle, bytes, buf);
						} else if (err == ERROR_BROKEN_PIPE || err == WSAENOTSOCK) {
							/* Connection closed or socket broken as EOF*/
							if (handle->flags & UV_HANDLE_READING) {
								handle->flags &= ~UV_HANDLE_READING;
								DECREASE_ACTIVE_COUNT(loop, handle);
							}
							handle->flags |= UV_HANDLE_EOF;

							uv__set_error(loop, UV_EOF, ERROR_SUCCESS);
							handle->read_cb((uv_stream_t*)handle, -1, buf);
						} else {
							/* Ouch! serious error. */
							if (handle->flags & UV_HANDLE_READING) {
								handle->flags &= ~UV_HANDLE_READING;
								DECREASE_ACTIVE_COUNT(loop, handle);
							}
							handle->flags |= UV_HANDLE_EOF;

							if (err == WSAECONNABORTED) {
								/* Turn WSAECONNABORTED into UV_ECONNRESET to be consistent with */
								/* Unix. */
								uv__set_error(loop, UV_ECONNRESET, err);
							} else {
								uv__set_sys_error(loop, err);
							}

							handle->read_cb((uv_stream_t*)handle, -1, buf);
						}
						next = 0;
						break;
					}
				}

				if (next == 1) {
					// going on
					handle->read_cb((uv_stream_t*)handle, buf.len, buf);
				} else {
					break;
				}
			}

		done:
			/* Post another read if still reading and not closing. */
			if ((handle->flags & UV_HANDLE_READING) &&
				!(handle->flags & UV_HANDLE_READ_PENDING)) {
				uv_udt_queue_read(loop, handle);
			}
		}

#ifdef UDT_DEBUG
		  printf("%s.%d,"
				  " reqs_pending:%d,"
				  " activecnt:%d,"
				  " write_pending:%d,"
				  " shutdown_req:%d,"
				  " flags:0x%x\n",
				  __FUNCTION__, __LINE__,
				  handle->reqs_pending,
				  handle->activecnt,
				  handle->write_reqs_pending,
				  handle->shutdown_req,
				  handle->flags);
#endif

		///DECREASE_PENDING_REQ_COUNT(handle);
	}
}

// Process dummy poll request, then dispatch udt event
void uv_process_udt_poll_req(
		uv_loop_t* loop,
		uv_udt_t* handle,
		uv_req_t* req) {
	int udtev, optlen;
	char dummy;


#ifdef UDT_DEBUG
	int stats;
	stats = udt_getsockstate(handle->udtfd);
	printf("%s:%d, %s, osfd:%d, udtsocket:%d, stats:%d\n",
			__FUNCTION__, __LINE__,
			(handle->flags & UV_HANDLE_LISTENING) ? "server" : "client",
			handle->socket, handle->udtfd, stats);

	  printf("%s.%d,"
			  " reqs_pending:%d,"
			  " activecnt:%d,"
			  " write_pending:%d,"
			  " shutdown_req:%d,"
			  " flags:0x%x,"
			  " poll_type:0x%x\n",
			  __FUNCTION__, __LINE__,
			  handle->reqs_pending,
			  handle->activecnt,
			  handle->write_reqs_pending,
			  handle->shutdown_req,
			  handle->flags,
			  req->udtflag);
#endif

	assert(handle->type == UV_UDT);
	assert(req->type == UV_UDT_POLL);

	// 1.
	// consume osfd event once
	if (req->udtflag & UV_UDT_REQ_POLL) {
		recv(handle->socket, &dummy, sizeof(dummy), 0);
	}

	// 2.
	// mask out handle flag for request type
	handle->udtflag &= ~req->udtflag;

	// 3.
	// decrease pending request count
	if ((req->udtflag & (UV_UDT_REQ_POLL | UV_UDT_REQ_POLL_ERROR)) &&
		handle->reqs_pending)
		DECREASE_PENDING_REQ_COUNT(handle);

	// 4.
	// check UDT event
	if (udt_getsockopt(handle->udtfd, 0, UDT_UDT_EVENT, &udtev, &optlen) < 0) {
#ifdef UDT_DEBUG
		printf("UDT fatal Error: %s:%d\n", __FUNCTION__, __LINE__);
#endif

		// disable poll active
		if (handle->udtflag & UV_UDT_REQ_POLL_ACTIVE) {
			handle->udtflag &= ~UV_UDT_REQ_POLL_ACTIVE;
			DECREASE_ACTIVE_COUNT(loop, handle);
		}

		// fill dummy error event
		udtev = UDT_UDT_EPOLL_ERR;

		// check error anyway
		uv__set_sys_error(loop, uv_translate_udt_error());
	} else if (udtev & UDT_UDT_EPOLL_ERR) {
#ifdef UDT_DEBUG
		printf("UDT fatal Error: %s:%d\n", __FUNCTION__, __LINE__);
#endif

		// disable poll active
		if (handle->udtflag & UV_UDT_REQ_POLL_ACTIVE) {
			handle->udtflag &= ~UV_UDT_REQ_POLL_ACTIVE;
			DECREASE_ACTIVE_COUNT(loop, handle);
		}

		// check error anyway
		uv__set_sys_error(loop, uv_translate_udt_error());
	}

	// 6.
	// process request on listening socket
	if ((handle->flags & UV_HANDLE_LISTENING) &&
		(udtev & (UDT_UDT_EPOLL_IN | UDT_UDT_EPOLL_ERR))) {
		udt_process_reqs_udtaccept(loop, handle);
	}

	// 7.
	// ...

	// 8.
	// process request on connecting socket

	// 8.1
	// connect request
	if ((handle->flags & UV_HANDLE_BOUND) &&
		!(handle->flags & UV_HANDLE_CONNECTION) &&
		(udtev & (UDT_UDT_EPOLL_OUT | UDT_UDT_EPOLL_ERR))) {
		udt_process_reqs_udtconnect(loop, handle);
	}

	// 8.2
	// read request
	if ((handle->flags & UV_HANDLE_CONNECTION) &&
		/*(handle->flags & UV_HANDLE_READING) &&*/
		(handle->flags & UV_HANDLE_READ_PENDING) &&
		(udtev & (UDT_UDT_EPOLL_IN | UDT_UDT_EPOLL_ERR))) {
		udt_process_reqs_udtread(loop, handle);
	}

	// 8.3
	// write request
	if ((handle->flags & UV_HANDLE_CONNECTION) &&
		(handle->write_reqs_pending) &&
		(udtev & (UDT_UDT_EPOLL_OUT | UDT_UDT_EPOLL_ERR))) {
		udt_process_reqs_udtwrite(loop, handle);
	}


	// 4.
	// update UDT event again
	if (udt_getsockopt(handle->udtfd, 0, UDT_UDT_EVENT, &udtev, &optlen) < 0) {
#ifdef UDT_DEBUG
		printf("UDT fatal Error: %s:%d\n", __FUNCTION__, __LINE__);
#endif

		// disable poll active
		if (handle->udtflag & UV_UDT_REQ_POLL_ACTIVE) {
			handle->udtflag &= ~UV_UDT_REQ_POLL_ACTIVE;
			DECREASE_ACTIVE_COUNT(loop, handle);
		}

		// fill dummy error event
		udtev = UDT_UDT_EPOLL_ERR;

		// check error anyway
		uv__set_sys_error(loop, uv_translate_udt_error());
	} else if (udtev & UDT_UDT_EPOLL_ERR) {
#ifdef UDT_DEBUG
		printf("UDT fatal Error: %s:%d\n", __FUNCTION__, __LINE__);
#endif

		// disable poll active
		if (handle->udtflag & UV_UDT_REQ_POLL_ACTIVE) {
			handle->udtflag &= ~UV_UDT_REQ_POLL_ACTIVE;
			DECREASE_ACTIVE_COUNT(loop, handle);
		}

		// check error anyway
		uv__set_sys_error(loop, uv_translate_udt_error());
	}

	// 5.
	// re-queue polling request in good condition only
	if (req->udtflag & UV_UDT_REQ_POLL) {
		if (udtev & UDT_UDT_EPOLL_ERR) {
			// clear pending event
			/*if (handle->socket != INVALID_SOCKET) {
				while (recv(handle->socket, &dummy, sizeof(dummy), 0) > 0) {
                    printf(".");
				}
				closesocket(handle->socket);
				handle->socket = INVALID_SOCKET;
			}*/

			// game over in error case, when last error event coming
			///assert(handle->reqs_pending == 0);
			///uv_want_endgame(handle->loop, (uv_handle_t*)handle);
		} else {
			// going on next event
			uv_udt_queue_poll(loop, handle);
		}
	}

	// 9.
	// try to game over in error case anyway
	if (req->udtflag & UV_UDT_REQ_POLL_ERROR) {
		// clear pending event
		/*if (handle->socket != INVALID_SOCKET) {
			while (recv(handle->socket, &dummy, sizeof(dummy), 0) > 0) {
				printf(".");
			}
			closesocket(handle->socket);
			handle->socket = INVALID_SOCKET;
		}*/

		// game over in error case, when last error event coming
		///assert(handle->reqs_pending == 0);
		///uv_want_endgame(handle->loop, (uv_handle_t*)handle);
	}

#ifdef UDT_DEBUG
	stats = udt_getsockstate(handle->udtfd);
	printf("%s:%d, %s, osfd:%d, udtsocket:%d, stats:%d, event:%d\n",
			__FUNCTION__, __LINE__,
			(handle->flags & UV_HANDLE_LISTENING) ? "server" : "client",
			handle->socket, handle->udtfd, stats, udtev);

	printf("%s.%d,"
			" reqs_pending:%d,"
			" activecnt:%d,"
			" write_pending:%d,"
			" shutdown_req:%d,"
			" flags:0x%x,"
			" pending_reqs_tail_udtwrite:%d,"
			" pending_reqs_tail_udtconnect:%d,"
			" pending_reqs_tail_udtaccept:%d\n",
			__FUNCTION__, __LINE__,
			handle->reqs_pending,
			handle->activecnt,
			handle->write_reqs_pending,
			handle->shutdown_req,
			handle->flags,
			handle->pending_reqs_tail_udtwrite,
			handle->pending_reqs_tail_udtconnect,
			handle->pending_reqs_tail_udtaccept);
#endif
}


int uv_udt_write(uv_loop_t* loop, uv_write_t* req, uv_udt_t* handle,
    uv_buf_t bufs[], int bufcnt, uv_write_cb cb) {
  BOOL success;
  DWORD bytes, flags;
  uv_buf_t buf;
  int next, n, it, rc=0;
  char dummy;


  if (!(handle->flags & UV_HANDLE_CONNECTION)) {
    uv__set_sys_error(loop, WSAEINVAL);
    return -1;
  }

  if (handle->flags & UV_HANDLE_SHUTTING) {
    uv__set_sys_error(loop, WSAESHUTDOWN);
    return -1;
  }

  uv_req_init(loop, (uv_req_t*) req);
  req->type = UV_WRITE;
  req->handle = (uv_stream_t*) handle;
  req->cb = cb;

  // prepare bufs queue porting from unix/stream.c write2
  ////////////////////////////////////////////////////////////////////////////
  req->error = 0;

  if (bufcnt <= UV_REQ_BUFSML_SIZE)
	req->bufs = req->bufsml;
  else
	req->bufs = malloc(sizeof(uv_buf_t) * bufcnt);

  memcpy(req->bufs, bufs, bufcnt * sizeof(uv_buf_t));
  req->bufcnt = bufcnt;
  req->write_index = 0;
  req->queued_bytes = uv_count_bufs(bufs, bufcnt);
  ////////////////////////////////////////////////////////////////////////////

  /* Prepare the overlapped structure. */
  memset(&(req->overlapped), 0, sizeof(req->overlapped));

  // 0.
  // check if there is queue write before, then process it first
  if (handle->write_reqs_pending) {
#ifdef UDT_DEBUG
	  printf("%s.%d: write request on pending: %d\n",
			  __FUNCTION__, __LINE__,
			  handle->write_reqs_pending);
#endif

	  // 0.1
	  // process previous queued write
	  udt_process_reqs_udtwrite(loop, handle);

	  // 0.2
	  // if still not finish it, then queue current write
	  if (handle->write_reqs_pending) {
		  // 0.
		  // queue poll request
		  uv_udt_queue_poll(loop, handle);

		  // 0.2
		  // enqueue write request
		  ///handle->reqs_pending++;
		  ///printf("%s:%d, reqs_pending:%d\n", __FUNCTION__, __LINE__, handle->reqs_pending);
		  handle->write_reqs_pending++;
		  ///REGISTER_HANDLE_REQ(loop, handle, req);
		  handle->write_queue_size += req->queued_bytes;

		  udt_insert_pending_req_udtwrite(loop, handle, (uv_req_t*) req);

		  return 0;
	  }
  }

  // 1.
  // try write on udt until got EAGAIN or send done
  next = 1;
  n = -1;
  for (it = 0; it < bufcnt; it ++) {
	  ULONG ilen = 0;

	  assert(bufs[it].len < 0x80000000); // avoid buf.len out of range
	  while (ilen < bufs[it].len) {
		  rc = udt_send(handle->udtfd, bufs[it].base+ilen, bufs[it].len-ilen, 0);
		  if (rc < 0) {
			  next = 0;
			  break;
		  } else {
			  if (n == -1) n = 0;
			  n += rc;
			  ilen += rc;
		  }
	  }
	  if (next == 0) break;
  }

#ifdef UDT_DEBUG
  printf("%s:%d, sent bytes:%d, queue bytes:%d\n",
		  __FUNCTION__, __LINE__,
		  n,
		  req->queued_bytes );
#endif

  // 2. then queue write request with the rest of buffer
  if (n == -1) {
      // nothing to send done, queue request with iocp or check errors
	  if (udt_getlasterror_code() == UDT_EASYNCSND) {
		  // 1.
		  // queue poll request
		  uv_udt_queue_poll(loop, handle);

		  // 2.
		  // enqueue write request
		  ///handle->reqs_pending++;
		  ///printf("%s:%d, reqs_pending:%d\n", __FUNCTION__, __LINE__, handle->reqs_pending);
		  handle->write_reqs_pending++;
		  ///REGISTER_HANDLE_REQ(loop, handle, req);
		  handle->write_queue_size += req->queued_bytes;

		  udt_insert_pending_req_udtwrite(loop, handle, (uv_req_t*) req);
	  } else {
		  uv__set_sys_error(loop, uv_translate_udt_error());
		  return -1;
	  }
  } else {
	  assert(n <= req->queued_bytes);

	  if (n == req->queued_bytes) {
		  // !!! send done, deliver request immediately

		  // queue write request
		  req->queued_bytes = 0;
		  ///handle->reqs_pending++;
		  ///printf("%s:%d, reqs_pending:%d\n", __FUNCTION__, __LINE__, handle->reqs_pending);
		  handle->write_reqs_pending++;
		  ///REGISTER_HANDLE_REQ(loop, handle, req);
		  udt_insert_pending_req_udtwrite(loop, handle, (uv_req_t*) req);

		  // insert dummy write poll request in case the first finished write
		  if (!(handle->udtflag & UV_UDT_REQ_WRITE)) {
			  handle->udtflag |= UV_UDT_REQ_WRITE;
			  uv_insert_pending_req(loop, &handle->udtreq_write);
			  ///handle->reqs_pending++;
			  ///printf("%s:%d, reqs_pending:%d\n", __FUNCTION__, __LINE__, handle->reqs_pending);
		  }
	  } else {
          // partial sent done, queue write request
		  req->queued_bytes -= n;
		  handle->write_queue_size += req->queued_bytes;

		  // update bufs
		  while (n > 0) {
			  uv_buf_t* buf = &(req->bufs[req->write_index]);
			  ULONG len = buf->len;

			  assert(req->write_index < req->bufcnt);

			  if ((ULONG)n < len) {
				  buf->base += n;
				  buf->len -= n;

				  n = 0;
			  } else {
				  /* Finished writing the buf at index req->write_index. */
				  req->write_index++;

				  n -= len;
			  }
		  }

		  // check error status
		  if (rc < 0) {
			  if (udt_getlasterror_code() == UDT_EASYNCSND) {
				  // 1.
				  // queue poll request
				  uv_udt_queue_poll(loop, handle);

				  // 2.
				  // enqueue write request
				  ///handle->reqs_pending++;
				  ///printf("%s:%d, reqs_pending:%d\n", __FUNCTION__, __LINE__, handle->reqs_pending);
				  handle->write_reqs_pending++;
				  ///REGISTER_HANDLE_REQ(loop, handle, req);

				  udt_insert_pending_req_udtwrite(loop, handle, (uv_req_t*) req);
			  } else {
				  uv__set_sys_error(loop, uv_translate_udt_error());
				  return -1;
			  }
		  }
	  }
  }

  return 0;
}


int uv_udt_nodelay(uv_udt_t* handle, int enable) {
  if (handle->socket != INVALID_SOCKET &&
      uv__udt_nodelay(handle, handle->socket, enable)) {
    return -1;
  }

  if (enable) {
    handle->flags |= UV_HANDLE_TCP_NODELAY;
  } else {
    handle->flags &= ~UV_HANDLE_TCP_NODELAY;
  }

  return 0;
}


int uv_udt_keepalive(uv_udt_t* handle, int enable, unsigned int delay) {
  if (handle->socket != INVALID_SOCKET &&
      uv__udt_keepalive(handle, handle->socket, enable, delay)) {
    return -1;
  }

  if (enable) {
    handle->flags |= UV_HANDLE_TCP_KEEPALIVE;
  } else {
    handle->flags &= ~UV_HANDLE_TCP_KEEPALIVE;
  }

  /* TODO: Store delay if handle->socket isn't created yet. */

  return 0;
}


int uv_udt_setrendez(uv_udt_t* handle, int enable) {
    int rndz = enable ? 1 : 0;
    
    if (handle->socket != INVALID_SOCKET &&
		udt_setsockopt(handle->udtfd, 0, UDT_UDT_RENDEZVOUS, &rndz, sizeof(rndz)))
	    return -1;

	if (enable)
		handle->flags |= UV_HANDLE_UDT_RENDEZ;
	else
		handle->flags &= ~UV_HANDLE_UDT_RENDEZ;

	return 0;
}

int uv_udt_setqos(uv_udt_t* handle, int qos) {
    if (handle->socket != INVALID_SOCKET &&
        udt_setsockopt(handle->udtfd, 0, UDT_UDT_QOS, &qos, sizeof(qos)))
	    return -1;

	return 0;
}

int uv_udt_setmbw(uv_udt_t* handle, int64_t mbw) {
    if (handle->socket != INVALID_SOCKET &&
        udt_setsockopt(handle->udtfd, 0, UDT_UDT_MAXBW, &mbw, sizeof(mbw)))
	    return -1;

	return 0;
}

int uv_udt_setmbs(uv_udt_t* handle, int32_t mfc, int32_t mudt, int32_t mudp) {
    if (handle->socket != INVALID_SOCKET &&
        ((mfc  != -1 ? udt_setsockopt(handle->udtfd, 0, UDT_UDT_FC,     &mfc,  sizeof(mfc))  : 0) ||
         (mudt != -1 ? udt_setsockopt(handle->udtfd, 0, UDT_UDT_SNDBUF, &mudt, sizeof(mudt)) : 0) ||
         (mudt != -1 ? udt_setsockopt(handle->udtfd, 0, UDT_UDT_RCVBUF, &mudt, sizeof(mudt)) : 0) ||
         (mudp != -1 ? udt_setsockopt(handle->udtfd, 0, UDT_UDP_SNDBUF, &mudp, sizeof(mudp)) : 0) ||
         (mudp != -1 ? udt_setsockopt(handle->udtfd, 0, UDT_UDP_RCVBUF, &mudp, sizeof(mudp)) : 0)))
	    return -1;

	return 0;
}

int uv_udt_setsec(uv_udt_t* handle, int32_t mode, unsigned char key_buf[], int32_t key_len) {
    if (handle->socket != INVALID_SOCKET &&
        (udt_setsockopt(handle->udtfd, 0, UDT_UDT_SECKEY, key_buf, (32 < key_len) ? 32 : key_len)) ||
         udt_setsockopt(handle->udtfd, 0, UDT_UDT_SECMOD, &mode, sizeof(mode)))
	    return -1;

	return 0;
}

int uv_udt_punchhole(uv_udt_t* handle, struct sockaddr_in address, int32_t from, int32_t to) {
	if (handle->socket != INVALID_SOCKET &&
        udt_punchhole(handle->udtfd, &address, sizeof(address), from, to))
		return -1;

	return 0;
}

int uv_udt_punchhole6(uv_udt_t* handle, struct sockaddr_in6 address, int32_t from, int32_t to) {
	if (handle->socket != INVALID_SOCKET &&
        udt_punchhole(handle->udtfd, &address, sizeof(address), from,  to))
		return -1;

	return 0;
}

int uv_udt_getperf(uv_udt_t* handle, uv_netperf_t* perf, int clear) {
	UDT_TRACEINFO lperf;

	memset(&lperf, 0, sizeof(lperf));
	if (handle->socket != INVALID_SOCKET &&
        udt_perfmon(handle->udtfd, &lperf, clear))
		return -1;

	// transform UDT local performance data
    // notes: it's same
    memcpy(perf, &lperf, sizeof(*perf));

	return 0;
}


int uv_udt_duplicate_socket(uv_udt_t* handle, int pid,
		LPWSAPROTOCOL_INFOW protocol_info) {
#ifdef UDT_DEBUG
	printf("Not support %s\n", __FUNCTION__);
#endif
	return -1;
}


void uv_udt_close(uv_loop_t* loop, uv_udt_t* udt) {
  if (udt->flags & UV_HANDLE_READING) {
    udt->flags &= ~UV_HANDLE_READING;
    DECREASE_ACTIVE_COUNT(loop, udt);
  }

  if (udt->flags & UV_HANDLE_LISTENING) {
    udt->flags &= ~UV_HANDLE_LISTENING;
    DECREASE_ACTIVE_COUNT(loop, udt);
  }

  if (!(udt->flags & UV_HANDLE_TCP_SOCKET_CLOSED)) {
	udt_close(udt->udtfd);
    udt->flags |= UV_HANDLE_TCP_SOCKET_CLOSED;
  }

  if (udt->udtflag & UV_UDT_REQ_POLL_ACTIVE) {
    udt->udtflag &= ~UV_UDT_REQ_POLL_ACTIVE;
    DECREASE_ACTIVE_COUNT(loop, udt);
  }
  uv__handle_start(udt);

#ifdef UDT_DEBUG
  printf("%s.%d,"
		  " reqs_pending:%d,"
		  " activecnt:%d,"
		  " write_pending:%d,"
		  " shutdown_req:%d,"
		  " flags:0x%x\n",
		  __FUNCTION__, __LINE__,
		  udt->reqs_pending,
		  udt->activecnt,
		  udt->write_reqs_pending,
		  udt->shutdown_req,
		  udt->flags);
#endif

  if (udt->reqs_pending == 0) {
    uv_want_endgame(udt->loop, (uv_handle_t*)udt);
  }
}

// udt error translation to syserr in windows
/*
uv_err_code uv_translate_sys_error(int sys_errno) {
  switch (sys_errno) {
    case ERROR_SUCCESS:                     return UV_OK;
    case ERROR_BEGINNING_OF_MEDIA:          return UV_EIO;
    case ERROR_BUS_RESET:                   return UV_EIO;
    case ERROR_CRC:                         return UV_EIO;
    case ERROR_DEVICE_DOOR_OPEN:            return UV_EIO;
    case ERROR_DEVICE_REQUIRES_CLEANING:    return UV_EIO;
    case ERROR_DISK_CORRUPT:                return UV_EIO;
    case ERROR_EOM_OVERFLOW:                return UV_EIO;
    case ERROR_FILEMARK_DETECTED:           return UV_EIO;
    case ERROR_INVALID_BLOCK_LENGTH:        return UV_EIO;
    case ERROR_IO_DEVICE:                   return UV_EIO;
    case ERROR_NO_DATA_DETECTED:            return UV_EIO;
    case ERROR_NO_SIGNAL_SENT:              return UV_EIO;
    case ERROR_OPEN_FAILED:                 return UV_EIO;
    case ERROR_SETMARK_DETECTED:            return UV_EIO;
    case ERROR_SIGNAL_REFUSED:              return UV_EIO;
    case ERROR_FILE_NOT_FOUND:              return UV_ENOENT;
    case ERROR_INVALID_NAME:                return UV_ENOENT;
    case ERROR_INVALID_REPARSE_DATA:        return UV_ENOENT;
    case ERROR_MOD_NOT_FOUND:               return UV_ENOENT;
    case ERROR_PATH_NOT_FOUND:              return UV_ENOENT;
    case WSANO_DATA:                        return UV_ENOENT;
    case ERROR_ACCESS_DENIED:               return UV_EPERM;
    case ERROR_PRIVILEGE_NOT_HELD:          return UV_EPERM;
    case ERROR_NOACCESS:                    return UV_EACCES;
    case WSAEACCES:                         return UV_EACCES;
    case ERROR_ADDRESS_ALREADY_ASSOCIATED:  return UV_EADDRINUSE;
    case WSAEADDRINUSE:                     return UV_EADDRINUSE;
    case WSAEADDRNOTAVAIL:                  return UV_EADDRNOTAVAIL;
    case WSAEAFNOSUPPORT:                   return UV_EAFNOSUPPORT;
    case WSAEWOULDBLOCK:                    return UV_EAGAIN;
    case WSAEALREADY:                       return UV_EALREADY;
    case ERROR_LOCK_VIOLATION:              return UV_EBUSY;
    case ERROR_SHARING_VIOLATION:           return UV_EBUSY;
    case ERROR_CONNECTION_ABORTED:          return UV_ECONNABORTED;
    case WSAECONNABORTED:                   return UV_ECONNABORTED;
    case ERROR_CONNECTION_REFUSED:          return UV_ECONNREFUSED;
    case WSAECONNREFUSED:                   return UV_ECONNREFUSED;
    case ERROR_NETNAME_DELETED:             return UV_ECONNRESET;
    case WSAECONNRESET:                     return UV_ECONNRESET;
    case ERROR_ALREADY_EXISTS:              return UV_EEXIST;
    case ERROR_FILE_EXISTS:                 return UV_EEXIST;
    case WSAEFAULT:                         return UV_EFAULT;
    case ERROR_HOST_UNREACHABLE:            return UV_EHOSTUNREACH;
    case WSAEHOSTUNREACH:                   return UV_EHOSTUNREACH;
    case ERROR_OPERATION_ABORTED:           return UV_EINTR;
    case WSAEINTR:                          return UV_EINTR;
    case ERROR_INVALID_DATA:                return UV_EINVAL;
    case ERROR_SYMLINK_NOT_SUPPORTED:       return UV_EINVAL;
    case WSAEINVAL:                         return UV_EINVAL;
    case ERROR_CANT_RESOLVE_FILENAME:       return UV_ELOOP;
    case ERROR_TOO_MANY_OPEN_FILES:         return UV_EMFILE;
    case WSAEMFILE:                         return UV_EMFILE;
    case WSAEMSGSIZE:                       return UV_EMSGSIZE;
    case ERROR_FILENAME_EXCED_RANGE:        return UV_ENAMETOOLONG;
    case ERROR_NETWORK_UNREACHABLE:         return UV_ENETUNREACH;
    case WSAENETUNREACH:                    return UV_ENETUNREACH;
    case WSAENOBUFS:                        return UV_ENOBUFS;
    case ERROR_OUTOFMEMORY:                 return UV_ENOMEM;
    case ERROR_CANNOT_MAKE:                 return UV_ENOSPC;
    case ERROR_DISK_FULL:                   return UV_ENOSPC;
    case ERROR_EA_TABLE_FULL:               return UV_ENOSPC;
    case ERROR_END_OF_MEDIA:                return UV_ENOSPC;
    case ERROR_HANDLE_DISK_FULL:            return UV_ENOSPC;
    case ERROR_WRITE_PROTECT:               return UV_EROFS;
    case ERROR_NOT_CONNECTED:               return UV_ENOTCONN;
    case WSAENOTCONN:                       return UV_ENOTCONN;
    case ERROR_DIR_NOT_EMPTY:               return UV_ENOTEMPTY;
    case ERROR_NOT_SUPPORTED:               return UV_ENOTSUP;
    case ERROR_INSUFFICIENT_BUFFER:         return UV_EINVAL;
    case ERROR_INVALID_FLAGS:               return UV_EBADF;
    case ERROR_INVALID_HANDLE:              return UV_EBADF;
    case ERROR_INVALID_PARAMETER:           return UV_EINVAL;
    case ERROR_NO_UNICODE_TRANSLATION:      return UV_ECHARSET;
    case ERROR_BROKEN_PIPE:                 return UV_EOF;
    case ERROR_BAD_PIPE:                    return UV_EPIPE;
    case ERROR_NO_DATA:                     return UV_EPIPE;
    case ERROR_PIPE_NOT_CONNECTED:          return UV_EPIPE;
    case ERROR_PIPE_BUSY:                   return UV_EBUSY;
    case ERROR_SEM_TIMEOUT:                 return UV_ETIMEDOUT;
    case WSAETIMEDOUT:                      return UV_ETIMEDOUT;
    case WSAHOST_NOT_FOUND:                 return UV_ENOENT;
    case WSAENOTSOCK:                       return UV_ENOTSOCK;
    case ERROR_NOT_SAME_DEVICE:             return UV_EXDEV;
    default:                                return UV_UNKNOWN;
  }
}
*/

int uv_translate_udt_error() {
#ifdef UDT_DEBUG
	printf("func:%s, line:%d, errno: %d, %s\n", __FUNCTION__, __LINE__, udt_getlasterror_code(), udt_getlasterror_desc());
#endif

	switch (udt_getlasterror_code()) {
	case UDT_SUCCESS: return ERROR_SUCCESS;

	case UDT_EFILE: return ERROR_IO_DEVICE;

	case UDT_ERDPERM:
	case UDT_EWRPERM: return ERROR_ACCESS_DENIED;

	//case ENOSYS: return UV_ENOSYS;

	case UDT_ESOCKFAIL:
	case UDT_EINVSOCK: return WSAENOTSOCK;

	//case ENOENT: return UV_ENOENT;
	//case EACCES: return UV_EACCES;
	//case EAFNOSUPPORT: return UV_EAFNOSUPPORT;
	//case EBADF: return UV_EBADF;
	//case EPIPE: return UV_EPIPE;

	case UDT_EASYNCSND:
	case UDT_EASYNCRCV: return WSAEWOULDBLOCK;

	case UDT_ECONNSETUP:
	case UDT_ECONNFAIL: return WSAECONNRESET;

	//case EFAULT: return UV_EFAULT;
	//case EMFILE: return UV_EMFILE;

	case UDT_ELARGEMSG: return WSAEMSGSIZE;

	//case ENAMETOOLONG: return UV_ENAMETOOLONG;

	///case UDT_EINVSOCK: return EINVAL;

	//case ENETUNREACH: return UV_ENETUNREACH;
	//case ERROR_BROKEN_PIPE: return UV_EOF;
	case UDT_ECONNLOST: return ERROR_BROKEN_PIPE;

	//case ELOOP: return UV_ELOOP;

	case UDT_ECONNREJ: return WSAECONNREFUSED;

	case UDT_EBOUNDSOCK: return WSAEADDRINUSE;

	//case EADDRNOTAVAIL: return UV_EADDRNOTAVAIL;
	//case ENOTDIR: return UV_ENOTDIR;
	//case EISDIR: return UV_EISDIR;
	case UDT_ENOCONN: return WSAENOTCONN;

	//case EEXIST: return UV_EEXIST;
	//case EHOSTUNREACH: return UV_EHOSTUNREACH;
	//case EAI_NONAME: return UV_ENOENT;
	//case ESRCH: return UV_ESRCH;

	case UDT_ETIMEOUT: return WSAETIMEDOUT;

	//case EXDEV: return UV_EXDEV;
	//case EBUSY: return UV_EBUSY;
	//case ENOTEMPTY: return UV_ENOTEMPTY;
	//case ENOSPC: return UV_ENOSPC;
	//case EROFS: return UV_EROFS;
	//case ENOMEM: return UV_ENOMEM;

	default: return -1;
	}
}

