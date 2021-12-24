// This is a fastcgi server that listens on port 8080 and responds to all
// requests with some text, including the request headers as JSON.

const tracer = require('dd-trace').init();
const fastcgi = require('node-fastcgi');

// In order for the span(s) associated with an HTTP request to be considered
// finished, the body of the response corresponding to the request must have
// ended.
const ignoreRequestBody = request => {
  request.on('data', () => {});
  request.on('end', () => {});
}

const requestListener = function (request, response) {
  ignoreRequestBody(request);
  response.writeHead(200, {'X-You-Better-Believe-It': 'foobar bearclaw'});
  response.end('You hit the fastcgi node script, congrats. Here are your headers:\n\n'
      + JSON.stringify(request.headers, null, 2));
}

console.log('fastcgi node.js web server is running');
const server = fastcgi.createServer(tracer.wrap('fastcgi.request', requestListener));
server.listen(8080);
