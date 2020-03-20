
var udt = require('udt');
var cln = udt.connect({port:51686, host: 'localhost'} , function(){
//var cln = udt.connect({port:51686, host: '192.168.1.106'} , function(){
    console.log('you can type char here, then server send it back:\n');
    process.stdin.resume();
    process.stdin.pipe(cln);   
    cln.pipe(process.stdout); 
});

var cln1 = udt.connect({port:51868, host: 'localhost'} , function(){
//var cln1 = udt.connect({port:51868, host: '192.168.1.106'} , function(){
    console.log('you can type char here, then server send it back:\n');
    process.stdin.resume();
    process.stdin.pipe(cln);   
    cln.pipe(process.stdout); 
});
