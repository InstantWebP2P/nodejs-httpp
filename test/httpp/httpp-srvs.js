var httpp = require('httpp');
var http = require('http');
var maxSrvs = 10;


// concurrent servers
for (var i = 0; i < maxSrvs; i ++) {
  var port = 51680+i;

  // httpp servers
  var srv = httpp.createServer(function(req, res){
    res.end('Hi, just say hi to you over UDP ...\n');
  });
  srv.listen(port);
  console.log('HTTPP server listing on UDP port '+port);

  // http servers
  var srv1 = http.createServer(function(req, res){
    res.end('Hi, just say hi to you over TCP ...\n');
  });
  srv1.listen(port);
  console.log('HTTP server listing on TCP port '+port);
}

