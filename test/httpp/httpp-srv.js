var httpp = require('httpp');
var srv = httpp.createServer(function(req, res){
  res.end('Hi, just say hi to you over UDP ...\n');
});
srv.listen(51680);
console.log('HTTPP server listing on UDP port 51680');

