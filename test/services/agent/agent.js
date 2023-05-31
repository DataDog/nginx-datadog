// This is an HTTP server that listens on port 8126, and prints to standard
// output a JSON representation of all traces that it receives.

const http = require('http');
const msgpack = require('massagepack');
const process = require('process');

function handleTraceSegments(segments) {
    console.log(msgpack.encodeJSON(segments));
}

const requestListener = function (request, response) {
  if (!request.url.endsWith('/traces')) {
    // The agent also supports telemetry endpoints.
    // We serve the [...]/traces endpoints only.
    response.writeHead(404);
    response.end();
    return;
  }

  let body = [];
  request.on('data', chunk => {
    // console.log('Received a chunk of data.');
    body.push(chunk);
  }).on('end', () => {
    // console.log('Received end of request.');
    body = Buffer.concat(body);
    const trace_segments = msgpack.decode(body);
    handleTraceSegments(trace_segments);
    response.writeHead(200);
    response.end(JSON.stringify({}));
  });
};

const port = 8126;
console.log(`node.js web server (agent) is running on port ${port}`);
const server = http.createServer(requestListener);
server.listen(port);

// In order for the span(s) associated with an HTTP request to be considered
// finished, the body of the response corresponding to the request must have
// ended.
const ignoreRequestBody = request => {
  request.on('data', () => {});
  request.on('end', () => {});
}

const adminListener = function (request, response) {
  ignoreRequestBody(request);

  // Token used to correlate this request with the log message that it will
  // produce.
  const token = request.headers['x-datadog-test-sync-token'];
  console.log(`SYNC ${token}`);

  response.writeHead(200);
  response.end();
};

const adminPort = 8888;
console.log(`node.js web server (agent admin) is running on port ${adminPort}`);
const admin = http.createServer(adminListener);
admin.listen(adminPort);

process.on('SIGTERM', function () {
  console.log('Received SIGTERM');

  let remaining = 2;
  function callback() {
    if (--remaining === 0) {
      process.exit(0);
    }
  }

  admin.close(callback);
  server.close(callback);
});
