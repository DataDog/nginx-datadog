# Ingress-nginx support
Ingress-nginx is an [ingress controller for Kubernetes](https://kubernetes.io/docs/concepts/services-networking/ingress/) that
uses NGINX as a reverse proxy and load balancer. By default, a Kubernetes cluster is isolated from the outside world for 
security reasons. An ingress controller allows external connections to the cluster based on defined ingress rules.

Ingress-nginx is configured and managed through [Kubernetes resources](https://kubernetes.io/docs/concepts/extend-kubernetes/api-extension/custom-resources/),
offering limited flexibility to modify the underlying NGINX configuration beyond its intended use case. However, ingress-nginx offers the possibility 

In the Kubernetes ecosystem, init containers are specialized containers that run before the main application containers in a Pod.
This mechanism can be used to add functionalities to an app container. For our purposes, the init container needs to:
Contain a compatible version of Datadog’s NGINX module for ingress-nginx.
During its run:
Copy the module to a shared volume.
The user must manually add the init container to their Kubernetes deployment.

Goal: Explain how it works and how to use.

Needs:
- Usage and usage examples
- Explain the loading mechanism and how to setup configs
- Relationship with OpenTelemetry (maybe offer drop-in support?)

For contributors:
- Why it needs to be patches?
- How to build/test/package?

Next steps:
- Reach out ingress-nginx to simplify building extra modules
(some kind of prepare step downloading nginx and apply patches)



