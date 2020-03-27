var httpps = require('httpps');
var fs = require('fs');

var options = {
     key: fs.readFileSync('test-key.pem'),
    cert: fs.readFileSync('test-cert.pem')
};

var srv = httpps.createServer(options, function(req, res){
    setTimeout(function(){
        res.end('Hi, just say hi to you over secure UDP ...\n');
    }, 6000);
});
srv.listen(51680);
console.log('HTTPPS server listing on UDP port 51680');

