# Ingress-nginx with Datadog

This example demonstrates how to integrate Datadog's tracing capabilities into the `ingress-nginx`
controller. By using the Datadog Nginx module, you can enable tracing for requests passing through
the Ingress controller, allowing you to monitor the performance of your applications in real time
with Datadog APM.

## End-to-end test with Docker Desktop

Add the `ingress-nginx` repository in your local Helm configuration:

```sh
$ helm repo add ingress-nginx https://kubernetes.github.io/ingress-nginx
```

In Docker Desktop, go the 'Kubernetes' tab, and create a cluster. Then use it:

```sh
$ kubectl config use-context docker-desktop
```

Install `ingress-nginx` (with the Datadog module):

```sh
$ helm install ingress-nginx ingress-nginx/ingress-nginx -f helm/values.yaml
```

Or, if you already did it once and want to take into account changes:

```sh
$ helm upgrade ingress-nginx ingress-nginx/ingress-nginx -f helm/values.yaml
```

Check the Ingress Nginx Controller pod is running:

```sh
$ kubectl get pods
NAME                           READY   STATUS
ingress-nginx-controller-[…]   1/1     Running
```

Check the Datadog module is present:

```sh
$ kubectl exec deploy/ingress-nginx-controller -- ls -la /modules_mount/
[…]
ngx_http_datadog_module.so
```

Check the Nginx configuration:

```sh
$ kubectl exec deploy/ingress-nginx-controller -- cat /etc/nginx/nginx.conf | grep load_module
[…]
load_module /modules_mount/ngx_http_datadog_module.so;
```

Deploy the test application:

```sh
$ kubectl apply -f test-application.yaml
```

Check the test application pod is running:
```sh
$ kubectl get pods
NAME              READY   STATUS
echo-server-[…]   1/1     Running
```

Send a test request:

```sh
$ curl -H "Host: echo.local" http://localhost/ -s | jq | grep datadog
      "x-datadog-trace-id": […],
      "x-datadog-parent-id": […],
      "x-datadog-sampling-priority": "1",
      "x-datadog-tags": […],
```

## Helm Deployment

You can deploy `ingress-nginx` with Datadog tracing enabled using Helm.
To customize the deployment, update the relevant parameters in your `values.yaml` file.

```sh
$ helm install ingress-nginx ingress-nginx/ingress-nginx -f helm/values.yaml
```

## Kubernetes Deployment

To manually deploy the `ingress-nginx` controller with Datadog tracing:

1. Deploy the default `ingress-nginx` controller:

```sh
$ kubectl apply -f https://raw.githubusercontent.com/kubernetes/ingress-nginx/refs/tags/controller-v1.11.3/deploy/static/provider/cloud/deploy.yaml
```

2. **Modify the controller spec**: You need to add an init container to load the Datadog module into the Nginx environment.
Here's an example of the changes required in the ingress-nginx controller deployment:

```yaml
spec:
  template:
    spec:
      initContainers:
        - name: init-datadog
          image: datadog/ingress-nginx-injection:v1.10.0
          command: ['/datadog/init_module.sh', '/opt/datadog-modules']
          volumeMounts:
            - name: nginx-module
              mountPath: /opt/datadog-modules
      containers:
        - name: controller
          image: registry.k8s.io/ingress-nginx/controller:v1.10.0
          env:
            - ...
            - name: DD_AGENT_HOST
              # or another way to access the datadog-agent
              # see https://docs.datadoghq.com/containers/kubernetes/installation/
              valueFrom:
                fieldRef:
                  fieldPath: status.hostIP
            - name: DD_SERVICE
              value: ingress-nginx
            - name: DD_ENV
              value: ...
            - name: DD_VERSION
              value: v1.10.0
          ...
          volumeMounts:
            - ...
            - mountPath: /opt/datadog-modules
              name: nginx-module
              readOnly: true
      volumes:
        - ...
        - name: nginx-module
          emptyDir: {}
```

3. **Apply the ConfigMap**: The following ConfigMap will add the necessary directive to load the Datadog module into Nginx:

```sh
$ kubectl apply -f k8s/configmap.yaml
```

This configuration will ensure that the Datadog module is loaded and ready to trace requests.
