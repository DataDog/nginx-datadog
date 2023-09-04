from flask import Flask, request, jsonify

app = Flask(__name__)

@app.route("/", defaults={"path": ""})
@app.route("/<string:path>")
@app.route("/<path:path>")
def main(path):
   response = {
      "service": "uwsgi",
      "headers": dict(request.headers)
   }
   print(response)
   return jsonify(response)
