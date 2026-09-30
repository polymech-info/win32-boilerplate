// WebView2 embed shell — output ../../dist/shared/viewer.html plus lazy viewer assets.
const path = require("path");
const HtmlWebpackPlugin = require("html-webpack-plugin");
const HtmlInlineScriptPlugin = require("html-inline-script-webpack-plugin");

module.exports = (env, argv) => {
  const isDev = argv.mode !== "production";
  return {
    entry: path.resolve(__dirname, "src", "embed.tsx"),
    output: {
      path: path.resolve(__dirname, "..", "..", "dist", "shared"),
      filename: "viewer.bundle.js",
      chunkFilename: "viewer.[name].[contenthash:8].js",
      assetModuleFilename: "viewer.[name][contenthash:8][ext]",
      clean: false,
      publicPath: "",
    },
    resolve: {
      extensions: [".tsx", ".ts", ".jsx", ".js"],
      alias: {
        "@": path.resolve(__dirname, "src"),
        // One physical copy of `three` for R3F + drei + dxf-viewer (avoids "Multiple instances" warning).
        three: path.resolve(__dirname, "node_modules", "three"),
      },
      fallback: {
        crypto: false,
        path: false,
      },
    },
    module: {
      rules: [
        {
          test: /\.m?js$/,
          include: [path.resolve(__dirname, "node_modules", "dxf-viewer")],
          resolve: { fullySpecified: false },
        },
        {
          test: /\.[jt]sx?$/,
          include: [path.resolve(__dirname, "src")],
          loader: "esbuild-loader",
          options: { loader: "tsx", target: "es2022", jsx: "automatic" },
        },
        {
          test: /\.css$/,
          use: ["style-loader", "css-loader", "postcss-loader"],
        },
        {
          test: /\.wasm$/,
          // Keep OpenCASCADE out of the 3D JS chunk until STEP/STP parsing needs it.
          type: "asset/resource",
        },
      ],
    },
    plugins: [
      new HtmlWebpackPlugin({
        template: path.resolve(__dirname, "embed.html"),
        filename: "viewer.html",
        inject: "body",
        minify: !isDev && {
          collapseWhitespace: true,
          removeComments: true,
          minifyCSS: true,
          minifyJS: true,
        },
      }),
      // Inline only the startup bundle; lazy viewer chunks remain sibling files on the vhost.
      ...(!isDev ? [new HtmlInlineScriptPlugin()] : []),
    ],
    devtool: isDev ? "eval-source-map" : false,
    performance: { hints: false },
    optimization: { splitChunks: false, runtimeChunk: false },
  };
};
