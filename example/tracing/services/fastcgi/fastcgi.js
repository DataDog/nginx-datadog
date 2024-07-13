// This is a fastcgi server that listens on port 8080 and responds to all
// requests with some text, including the request headers as JSON.

const fastcgi = require('node-fastcgi');
const process = require('process');

const requestListener = function (request, response) {
  const responseBody = JSON.stringify({
    "service": "fastcgi",
    "headers": request.headers
  }, null, 2);
  console.log(responseBody);
  response.end(responseBody);
};

console.log('fastcgi node.js web server is running');
const server = fastcgi.createServer(requestListener);
server.listen(8080);

process.on('SIGTERM', function () {
  console.log('Received SIGTERM');
  server.close(function () { process.exit(0); });
});
