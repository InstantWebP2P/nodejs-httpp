'use strict';

const udt = require('udt');
const srv = udt.createServer(function(socket){
    socket.pipe(socket);     
});

srv.listen(51686);
console.log('Listening on UDP port 51686');

