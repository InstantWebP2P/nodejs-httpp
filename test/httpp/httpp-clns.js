var httpp = require('httpp');
var http = require('http');
var maxSrvs = 10;
var maxClns = 20;


// concurrent clients
for (var i = 0; i < maxSrvs; i ++) {
  var port = 51680+i;
  
  for (var j = 0; j < maxClns; j ++) {
    // httpp clients
    httpp.get('http://localhost:'+port, function(res){
    //httpp.get('http://192.168.1.105:'+port, function(res){
      console.log('STATUS: ' + res.statusCode);
      console.log('HEADERS: ' + JSON.stringify(res.headers));
      res.on('data', function (chunk) {
        console.log('BODY: ' + chunk);
      });
    });
  
    // http clients
    http.get('http://localhost:'+port, function(res){
    //httpp.get('http://192.168.1.105:'+port, function(res){
      console.log('STATUS: ' + res.statusCode);
      console.log('HEADERS: ' + JSON.stringify(res.headers));
      res.on('data', function (chunk) {
        console.log('BODY: ' + chunk);
      });
    });
  }
}

