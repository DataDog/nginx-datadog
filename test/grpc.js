// This is a gRPC server that listens on port 8080.

const grpc = require('@grpc/grpc-js');
const protoLoader = require('@grpc/proto-loader');
const packageDefinition = protoLoader.loadSync(
    './grpc.proto',
    {keepCase: true,
     longs: String,
     enums: String,
     defaults: true,
     oneofs: true
    });
const proto = grpc.loadPackageDefinition(packageDefinition).helloworld;

function sayHello(call, callback) {
  console.log('processing request from ' + call.request.name);
  callback(null, {message: 'Congrats, ' + call.request.name + ', you hit the gRPC node script.'});
}

const server = new grpc.Server();
server.addService(proto.Greeter.service, {sayHello: sayHello});
server.bindAsync('0.0.0.0:8080', grpc.ServerCredentials.createInsecure(), () => {
  console.log('gRPC node server is about to run');
  server.start();
});
