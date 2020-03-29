var httpps = require('httpps');
var fs = require('fs');

var options = {
     key: fs.readFileSync('test-key.pem'),
    cert: fs.readFileSync('test-cert.pem')
};

var srv0 = httpps.createServer(options, function (req, res) {
    setTimeout(function () {
        res.end('Server/0 Hi, just say hi to you over secure UDP ...\n');
    }, 6000);
});
srv0.listen({port: 51680, opt: {reuseaddr: 0, reuseable: 1} }, function() {
    console.log('Server/0 listing on UDP port 51680');

    var srv1 = httpps.createServer(options, function (req, res) {
        setTimeout(function () {
            res.end('Server/1 Hi, just say hi to you over secure UDP ...\n');
        }, 6000);
    });
    srv1.listen({ port: 51680, opt: { reuseaddr: 0, reuseable: 1 } }, function() {
        console.log('Server/1 listing on UDP port 51680');
        
        var srv2 = httpps.createServer(options, function (req, res) {
            setTimeout(function () {
                res.end('Server/2 Hi, just say hi to you over secure UDP ...\n');
            }, 6000);
        });
        srv2.listen({ port: 51680, opt: { reuseaddr: 0, reuseable: 1 } }, function() {
            console.log('Server/2 listing on UDP port 51680');
        });
    });
});