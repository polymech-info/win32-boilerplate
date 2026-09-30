// Webpack: single self-contained HTML for WebView2 host popup.
//
// Bundles the Vite/React app via `src/embed.tsx` + `embed.html` (Preact compat
// aliases match `vite.config.ts`). Output: `../../dist/shared/xblox.html`.
//
// `npm run dev:embed` — webpack-dev-server on 5175 (Vite dev uses 5173).

const path = require("path");
const HtmlWebpackPlugin = require("html-webpack-plugin");
const HtmlInlineScriptPlugin = require("html-inline-script-webpack-plugin");

module.exports = (env, argv) => {
  const isDev = argv.mode !== "production";
  return {
    entry: path.resolve(__dirname, "src", "embed.tsx"),
    output: {
      path: path.resolve(__dirname, "..", "..", "dist", "shared"),
      filename: "xblox.bundle.js",
      // dist/shared may hold other shared runtime assets — never wipe the whole folder.
      clean: false,
      publicPath: "",
    },
    resolve: {
      extensions: [".tsx", ".ts", ".jsx", ".js", ".json"],
      modules: [path.resolve(__dirname, "node_modules"), "node_modules"],
      alias: {
        // Subpaths must come before the bare `react`/`react-dom` file aliases (see webpack #11909).
        "react/jsx-runtime": require.resolve("preact/compat/jsx-runtime"),
        "react/jsx-dev-runtime": require.resolve("preact/compat/jsx-dev-runtime"),
        "preact/jsx-runtime": require.resolve("preact/jsx-runtime"),
        "preact/hooks": require.resolve("preact/hooks"),
        "react-dom/client": require.resolve("preact/compat/client"),
        "react-dom": require.resolve("preact/compat"),
        react: require.resolve("preact/compat"),
        "@pm/shared": path.resolve(__dirname, "..", "shared"),
        "@": path.resolve(__dirname, "src"),
      },
      conditionNames: ["import", "module", "browser", "default"],
    },
    module: {
      rules: [
        {
          test: /\.[jt]sx?$/,
          include: [path.resolve(__dirname, "src"), path.resolve(__dirname, "..", "shared")],
          loader: "esbuild-loader",
          options: {
            loader: "tsx",
            target: "es2022",
            jsx: "automatic",
            jsxImportSource: "preact",
          },
        },
        {
          test: /\.css$/i,
          use: ["style-loader", "css-loader", "postcss-loader"],
        },
      ],
    },
    plugins: [
      new HtmlWebpackPlugin({
        template: path.resolve(__dirname, "embed.html"),
        filename: "xblox.html",
        inject: "body",
        minify: !isDev && {
          collapseWhitespace: true,
          removeComments: true,
          minifyCSS: true,
          minifyJS: true,
        },
      }),
      ...(!isDev ? [new HtmlInlineScriptPlugin()] : []),
    ],
    // Production uses inline maps so WebView2 only ships `chat.html` (no separate .map on vhost).
    devtool: isDev ? "eval-source-map" : "inline-source-map",
    devServer: {
      static: { directory: path.resolve(__dirname, "..", "..", "dist", "shared") },
      host: "127.0.0.1",
      port: 5175,
      allowedHosts: ["localhost", "127.0.0.1"],
      hot: true,
    },
    performance: { hints: false },
  };
};
