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
  if (request.url === '/auth') {
    const auth = request.headers.authorization;
    if (auth === 'mysecret') {
      response.writeHead(200);
      response.end('');
    } else {
      response.writeHead(401, { "content-type": "text/plain" });
      response.end('Unauthorized');
    }
    return;
  }

  const responseBody = JSON.stringify({
    "service": "http",
    "headers": request.headers
  }, null, 2);
  console.log(responseBody);

  // "[...]/status/<code>" makes us respond with status <code>.
  let status = 200;
  const match = request.url.match(/.*\/status\/([0-9]+)$/);
  if (match  !== null) {
    const [full, statusString] = match;
    status = Number.parseInt(statusString, 10);
  }
  response.writeHead(status);
  response.end(responseBody);
}

console.log('http node.js web server is running');
const server = http.createServer(requestListener);
server.listen(8080);

process.on('SIGTERM', function () {
  console.log('Received SIGTERM');
  server.close(function () { process.exit(0); });
});
