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

const sendRepeatResponse = (request, response) => {
  try {
    const urlObj = new URL(request.url, `http://${request.headers.host}`);
    const card = parseInt(urlObj.searchParams.get("card"));
    const numBouts = parseInt(urlObj.searchParams.get("num_bouts"));
    const delay = parseInt(urlObj.searchParams.get("delay"));
    if (isNaN(card) || isNaN(numBouts) || isNaN(delay)) {
      response.writeHead(400, { "Content-Type": "text/plain" });
      response.end("Invalid query parameters");
      return;
    }
    // Pre-calculate the output string once.
    const output = "Hello world!\n".repeat(card);
    response.writeHead(200, { "Content-Type": "text/plain" });
    let boutCount = 0;
    const sendBout = () => {
      response.write(output);
      boutCount++;
      if (boutCount >= numBouts) {
        response.end();
      } else {
        setTimeout(sendBout, delay);
      }
    }
    setTimeout(sendBout, delay);
  } catch (err) {
    response.writeHead(500, { "Content-Type": "text/plain" });
    response.end("Server error");
  }
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

  if (request.url.match(/.*\/repeat\/?(?:\?.*)?$/)) {
    sendRepeatResponse(request, response);
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
