var udt = require('udt');
var srv = udt.createServer(function(socket){
    socket.pipe(socket);     
});

srv.listen(51686);
console.log('Listening on UDP port 51686');

var srv1 = udt.createServer(function(socket){
    socket.pipe(socket);     
});

srv1.listen(51868);
console.log('Listening on UDP port 51868');
