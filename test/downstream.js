const http = require('http');

const requestListener = function (req, res) {
  res.writeHead(200);
  res.end('You hit the node script, congrats. Here are your headers:\n\n' + JSON.stringify(req.headers, null, 2));
}

console.log('Yes, I am alive.');
const server = http.createServer(requestListener);
server.listen(8080);
