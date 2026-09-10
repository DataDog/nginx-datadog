from flask import Flask, jsonify, request

app = Flask(__name__)


@app.get("/ready")
def ready():
    return "ready"


@app.get("/proxy/<path:route>")
def echo(route):
    return jsonify(path=request.path,
                   headers={
                       key.lower(): value
                       for key, value in request.headers.items()
                   })
