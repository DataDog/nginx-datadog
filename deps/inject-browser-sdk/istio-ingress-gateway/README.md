# Experimental

## Filter for Istio Ingress Gateway

### How to test

Follow the instructions on running the [httpbin sample app](https://istio.io/latest/docs/tasks/traffic-management/ingress/ingress-control/#before-you-begin) from Istio's site.

Then, adjust the RUM configuration in the `istio-envoy-filter.yml` file to match your application's.

Next, feed the recipe with `kubectl`:

```
kubectl apply -f - <<EOF
<Ctrl+V the yaml file>
EOF
```

Navigate to [http://localhost/html](http://localhost/html) and refresh until you see the changes in effect, and the tag in place.
