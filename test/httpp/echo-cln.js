
var udt = require('udt');
var cln = udt.connect({port:51686, host: 'localhost'} , function(){
    console.log('you can type char here, then server send it back:\n');
    process.stdin.resume();
    process.stdin.pipe(cln);   
    cln.pipe(process.stdout); 
});

