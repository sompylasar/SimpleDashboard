# Demo usage of React to implement an insights visualizer

To start, serve the files via an HTTP server (e.g. `npm install -g http-server && http-server ./`). Just opening the `index.html` in a browser won‘t work because the JSX files are loaded dynamically, and the browsers‘ cross-origin policy prohibits it for the `file` protocol.

The `index.html` is made as a simplest template. The `PAGE_TITLE` placeholder should be replaced with the page title. The `DATA_JSON` placeholder should be replaced with the JSON object (after resolving all the TODOs in `index.html`).
