// This is an HTTP server that listens on port 8126, and prints to standard
// output a JSON representation of all traces that it receives.

const http = require('http');
const msgpack = require('massagepack');
const process = require('process');

const requestListener = (() => {
  function handleTraceSegments(segments) {
    console.log(msgpack.encodeJSON(segments));
  }

  let next_rem_cfg_resp = undefined;
  let next_rem_cfg_version = -1;

  function version_from_resp(req_body) {
    const req_json = JSON.parse(req_body);
    const req_targets = req_json['targets'];
    const req_targets_decoded = Buffer.from(req_targets, 'base64').toString('utf-8');
    const req_targets_json = JSON.parse(req_targets_decoded);
    const req_signed = req_targets_json['signed'];
    return req_signed['version'];
  }

  function version_from_req(req_body) {
    const req_json = JSON.parse(req_body);
    return req_json['client']['state']['targets_version'];
  }

  return (request, response) => {
    if (request.url.endsWith('/traces')) {
      let body = [];
      request.on('data', chunk => {
        body.push(chunk);
      }).on('end', () => {
        body = Buffer.concat(body);
        const trace_segments = msgpack.decode(body);
        handleTraceSegments(trace_segments);
        response.writeHead(200);
        response.end(JSON.stringify({}));
      });
    } else if (request.url == '/v0.7/config') {
      let body = [];
      request.on('data', chunk => {
        body.push(chunk);
      }).on('end', () => {
        body = Buffer.concat(body);
        body = body.toString();
        const version = version_from_req(body)
        console.log("Remote config request with version " + version + ": " + body);
        response.writeHead(200);
        if (next_rem_cfg_resp === undefined) {
          response.end(JSON.stringify({a:1}));
        } else {
          response.end(next_rem_cfg_resp);
        }
      });
    } else if (request.url === '/save_rem_cfg_resp') {
        let body = [];
        request.on('data', chunk => {
            body.push(chunk);
        }).on('end', () => {
            body = Buffer.concat(body);
            next_rem_cfg_resp = body;
            next_rem_cfg_version = version_from_resp(next_rem_cfg_resp.toString());
            console.log("Next remote config response with version " + next_rem_cfg_resp +
                ": " + next_rem_cfg_version);
            response.writeHead(200);
            response.end();
        });
    } else {
      // The agent also supports telemetry endpoints.
      // But we don't servet those here.
      response.writeHead(404);
      response.end();
      return;
    }
  };
})();

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
