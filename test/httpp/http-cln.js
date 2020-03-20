var http = require('http');

for (var i = 0; i < 20; i ++)
http.get('http://localhost:51680', function(res){
   console.log('STATUS: ' + res.statusCode);
   console.log('HEADERS: ' + JSON.stringify(res.headers));
   res.on('data', function (chunk) {
   console.log('BODY: ' + chunk);
  });
});
