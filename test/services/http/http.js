// This is an HTTP server that listens on port 8080 and responds to all
// requests with some text, including the request headers as JSON.

const http = require('http');
const process = require('process');
const WebSocket = require('ws');

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

  // parameterized response body test endpoint
  if (request.url.match(/.*\/response_body_test\/?.*/)) {
    ignoreRequestBody(request);
    try {
      const urlObj = new URL(request.url, `http://${request.headers.host}`);
      const trigger = urlObj.searchParams.get("trigger");
      const format = urlObj.searchParams.get("format") || "json";
      const status = parseInt(urlObj.searchParams.get("status") || "200");

      // mapping to avoid WAF trigger words in query parameters
      const triggerMap = {
        "res_bod_tri": "response_body_trigger",
        "mat_val": "matched value",
        "mat_key": "matched key",
        "blo_def": "block_default",
        "blo_res_bod": "block_response_body",
        "safe": "safe_content"
      };

      const triggerWord = triggerMap[trigger] || "safe_content";

      let responseBody;
      let contentType;

      if (format === "text") {
        contentType = "text/plain";
        responseBody = `This response contains the ${triggerWord} keyword`;
      } else if (format === "html") {
        contentType = "text/html";
        responseBody = `<html><body>This response contains the <strong>${triggerWord}</strong> keyword</body></html>`;
      } else {
        contentType = "application/json";
        if (trigger === "mat_key") {
          responseBody = JSON.stringify({
            [triggerWord]: "some value",
            "other_key": "another value"
          });
        } else {
          responseBody = JSON.stringify({
            "message": triggerWord,
            "test": "response body"
          });
        }
      }

      // HEAD requests should also get a Content-Length
      const headers = {
        "content-type": contentType,
        "content-length": Buffer.byteLength(responseBody, 'utf8')
      };

      response.writeHead(status, headers);

      if (request.method === 'HEAD') {
        response.end();
      } else {
        response.end(responseBody);
      }
    } catch (err) {
      response.writeHead(400, { "content-type": "text/plain" });
      response.end("Invalid query parameters");
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

function reverseBytes(str) {
  return str.split('').reverse().join('');
}

console.log('http node.js web server is running');
const server = http.createServer(requestListener);
server.listen(8080);

const wss = new WebSocket.Server({ server });

wss.on('connection', (ws) => {
  ws.on('message', (message) => {
    const messageStr = message.toString();

    if (messageStr.length > 1024 || messageStr.length < 1) {
      return;
    }

    const reversed = reverseBytes(messageStr);
    ws.send(reversed);
  });
});

process.on('SIGTERM', function () {
  console.log('Received SIGTERM');
  server.close(function () { process.exit(0); });
});
