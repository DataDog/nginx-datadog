// This is an HTTP server that listens on port 8080 and responds to all
// requests with some text, including the request headers as JSON.

const http = require('http');
const process = require('process');

// In order for the span(s) associated with an HTTP request to be considered
// finished, the body of the response corresponding to the request must have
// ended.
const ignoreRequestBody = request => {
  request.on('data', () => {});
  request.on('end', () => {});
}

const requestListener = function (request, response) {
  ignoreRequestBody(request);
  
  response.writeHead(200, {
    'X-You-Better-Believe-It': 'foobar bearclaw',
    'X-Datadog-Sampling-Priority': '18'
  });
  response.end('You hit the http node script, congrats. Here are your headers:\n\n'
      + JSON.stringify(request.headers, null, 2));
}

console.log('http node.js web server is running');
const server = http.createServer(requestListener);
server.listen(8080);

process.on('SIGTERM', function () {
  console.log('Received SIGTERM');
  server.close(function () { process.exit(0); });
});
