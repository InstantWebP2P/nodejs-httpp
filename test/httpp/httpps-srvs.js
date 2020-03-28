var httpps = require('httpps');
var fs = require('fs');

var options = {
     key: fs.readFileSync('test-key.pem'),
    cert: fs.readFileSync('test-cert.pem')
};

for (var i = 0; i < 3; i ++) {
    var srv = httpps.createServer(options, function (req, res) {
        setTimeout(function () {
            res.end('Server/' + i +' Hi, just say hi to you over secure UDP ...\n');
        }, 6000);
    });

    srv.listen(51680);
    console.log('Server/' + i + ' listing on UDP port 51680');
}
