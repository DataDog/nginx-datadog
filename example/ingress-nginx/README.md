# Ingress-nginx with Datadog

This example demonstrates how to integrate Datadog's tracing capabilities into the 
`ingress-nginx` controller (v1.10+). By using the Datadog NGINX module, you can enable 
tracing for requests passing through the ingress controller, allowing you to monitor 
the performance of your applications in real time with Datadog APM.

## Helm Deployment

You can deploy `ingress-nginx` with Datadog tracing enabled using Helm.
To customize the deployment, update the relevant parameters in your `values.yaml` file.

```sh
helm install ingress-nginx ingress-nginx/ingress-nginx -f helm/values.yaml
```

## Kubernetes Deployment

To manually deploy the ingress-nginx controller with Datadog tracing:

1. Deploy the default ingress-nginx controller:

```sh
kubectl apply -f https://raw.githubusercontent.com/kubernetes/ingress-nginx/main/deploy/static/provider/cloud/deploy.yaml
```

2. **Modify the controller spec**: You need to add an init container to load the Datadog module into the NGINX environment. 
Here's an example of the changes required in the ingress-nginx controller deployment:

```yaml
spec:
  template:
    spec:
      initContainers:
        - name: init-datadog
          image: datadog/ingress-nginx-injection:v1.10.0
          volumeMounts:
            - name: nginx-module
              mountPath: /opt/datadog-modules
```

3. **Apply the ConfigMap**: The following ConfigMap will add the necessary directive to load the Datadog module into NGINX:

```sh
kubectl apply -f k8s/configmap.yaml
```

This configuration will ensure that the Datadog module is loaded and ready to trace requests

