// This is an HTTP server that listens on port 8080 and responds to all
// requests with some text, including the request headers as JSON.

const http = require('http');

const requestListener = function (request, response) {
  response.writeHead(200, {'X-You-Better-Believe-It': 'foobar bearclaw'});
  response.end('You hit the http node script, congrats. Here are your headers:\n\n'
      + JSON.stringify(request.headers, null, 2));
}

console.log('http node.js web server is running');
const server = http.createServer(requestListener);
server.listen(8080);
