use axum::body::{Body, Bytes};
use axum::http::{HeaderMap, HeaderValue, Request, Response};
use axum::middleware::{self, Next};
use axum::response::IntoResponse;
use axum::{
    extract::Path,
    http::StatusCode,
    routing::{get, post},
    Router,
};
use futures_util::StreamExt;
use serde::Deserialize;
use std::convert::Infallible;
use std::time::Duration;
use tokio::time::sleep;
use tower::ServiceBuilder;
use serde_json;

#[tokio::main]
async fn main() {
    // Create the router with routes
    let app = Router::new()
        .route("/get/:bout_size/:bout_count/:wait_ms", get(handle_get))
        .route(
            "/post/:bout_size/:bout_count/:wait_ms/:read_delay_msg",
            post(handle_post),
        )
        .route("/json/:size_kb", get(handle_json))
        .route("/auth", get(handle_auth))
        .layer(
            ServiceBuilder::new()
                .layer(middleware::from_fn(replace_status_code))
                .layer(middleware::from_fn(copy_accel_buffering)),
        );

    // Run the server
    axum::Server::bind(&"0.0.0.0:3000".parse().unwrap())
        .serve(app.into_make_service())
        .await
        .unwrap();
}

async fn handle_get(
    Path((bout_size, bout_count, wait_ms)): Path<(usize, usize, u64)>,
) -> Result<impl IntoResponse, StatusCode> {
    Ok(generate_response(
        "sample text".to_string(),
        bout_size,
        bout_count,
        wait_ms,
    ))
}

async fn handle_post(
    Path((bout_size, bout_count, wait_ms, read_delay_ms)): Path<(usize, usize, u64, u64)>,
    request: Request<axum::body::Body>,
) -> Result<impl IntoResponse, StatusCode> {
    let mut result = String::new();
    let mut stream = request.into_body();

    while let Some(chunk) = stream.next().await {
        let chunk = chunk.map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?;
        result.push_str(&String::from_utf8_lossy(&chunk));
        sleep(Duration::from_millis(read_delay_ms)).await;
    }

    Ok(generate_response(result, bout_size, bout_count, wait_ms))
}

async fn handle_json(
    Path(size_kb): Path<usize>,
) -> Result<impl IntoResponse, StatusCode> {
    // Generate a JSON response with specified size in KB
    let target_size = size_kb * 1024;

    // Create a large JSON object with repeated data
    let mut items = Vec::new();
    let item_size = 200; // Approximate size of each item in bytes
    let num_items = target_size / item_size;

    for i in 0..num_items {
        items.push(serde_json::json!({
            "id": i,
            "name": format!("Item {}", i),
            "description": "This is a sample item with some text to fill up space in the JSON response",
            "timestamp": 1234567890 + i,
            "active": i % 2 == 0,
            "metadata": {
                "category": "sample",
                "tags": ["tag1", "tag2", "tag3"],
                "score": i as f64 * 1.5
            }
        }));
    }

    let response = serde_json::json!({
        "status": "success",
        "total_items": num_items,
        "size_kb": size_kb,
        "data": items
    });

    Ok(axum::Json(response))
}

async fn handle_auth(headers: HeaderMap) -> StatusCode {
    let auth_header = headers.get("Authorization");
    if let Some(auth_header) = auth_header {
        let auth_header = auth_header.to_str().unwrap();
        if auth_header == "mysecret" {
            return StatusCode::OK;
        }
    }

    StatusCode::UNAUTHORIZED
}

fn generate_response(
    content: String,
    bout_size: usize,
    bout_count: usize,
    wait_ms: u64,
) -> Response<Body> {
    let mut repeat_text = content.repeat(bout_size);
    repeat_text.push('\n');
    let chunk = Bytes::from(repeat_text);

    let stream = futures_util::stream::iter(0..bout_count).then(move |_| {
        let chunk = chunk.clone();
        async move {
            sleep(Duration::from_millis(wait_ms)).await;
            Ok::<_, Infallible>(chunk)
        }
    });

    let body = Body::wrap_stream(stream);

    let mut resp = Response::new(body);
    resp.headers_mut().insert(
        axum::http::header::CONTENT_TYPE,
        HeaderValue::from_static("text/plain"),
    );
    resp
}

async fn copy_accel_buffering<B>(req: Request<B>, next: Next<B>) -> impl IntoResponse {
    let maybe_accel = req.headers().get("X-Accel-Buffering").cloned();

    let mut res = next.run(req).await;

    if let Some(accel_val) = maybe_accel {
        res.headers_mut()
            .insert("X-Accel-Buffering", accel_val.clone());
    }

    res
}

#[derive(Deserialize)]
struct QueryParams {
    status_code: Option<u16>,
}
async fn replace_status_code<B>(req: Request<B>, next: Next<B>) -> impl IntoResponse {
    if req.uri().path() == "/auth" {
        return next.run(req).await;
    }

    let query_params = req.uri().query().map(|str| {
        serde_urlencoded::from_str::<QueryParams>(str).unwrap_or(QueryParams { status_code: None })
    });
    match query_params {
        Some(QueryParams {
            status_code: Some(status_code),
        }) => {
            let mut res = next.run(req).await;
            *res.status_mut() =
                StatusCode::from_u16(status_code).unwrap_or(StatusCode::INTERNAL_SERVER_ERROR);
            res
        }
        _ => next.run(req).await,
    }
}
