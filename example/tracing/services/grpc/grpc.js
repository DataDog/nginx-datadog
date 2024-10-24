const grpc = require('@grpc/grpc-js');
const protoLoader = require('@grpc/proto-loader');
const reflection = require('grpc-node-server-reflection');
const wrapServerWithReflection = reflection.default;
const process = require('process');

const packageDefinition = protoLoader.loadSync(
    './grpc.proto',
    {keepCase: true,
     longs: String,
     enums: String,
     defaults: true,
     oneofs: true
    });
const proto = grpc.loadPackageDefinition(packageDefinition).upstream;

function getMetadata(call, callback) {
  const response = {service: 'grpc', metadata: call.metadata.getMap()};
  console.log(response);
  callback(null, response);
}

const server = wrapServerWithReflection(new grpc.Server());
server.addService(proto.Upstream.service, {GetMetadata: getMetadata});
server.bindAsync('0.0.0.0:1337', grpc.ServerCredentials.createInsecure(), () => {
  console.log('gRPC node server is about to run');
  server.start();
});

process.on('SIGTERM', function () {
  console.log('Received SIGTERM');
  server.tryShutdown(function () { process.exit(0); });
});
