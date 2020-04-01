var http = require('http');
var httpp = require('httpp');

var https = require('https');
var httpps = require('httpps');

http.createServer(function (request, response) {
  response.writeHead(200, {'Content-Type': 'text/plain'});
  response.end('Hello, AppNet:\n\tThis is Tom calling to Jagua from TCP.:)\n');
}).listen(3008);

console.log('Server running at http://127.0.0.1:3008/');

httpp.createServer(function (request, response) {
  response.writeHead(200, {'Content-Type': 'text/plain'});
  response.end('Hello, AppNet:\n\tThis is Tom calling to Jagua from UDP.:)\n');
}).listen(3008);

console.log('Server running at httpp://127.0.0.1:3008/');
