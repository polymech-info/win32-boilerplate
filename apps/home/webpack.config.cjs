// Webpack: single self-contained HTML for the WebView2 home surface.
//
// Bundles the Vite/React app via `src/embed.tsx` + `embed.html` (Preact compat
// aliases match `vite.config.ts`). Output: `../../dist/shared/home.html`.
//
// `npm run dev:embed` — webpack-dev-server on 5175 (Vite dev uses 5173).

const path = require("path");
const fs = require("fs");
const webpack = require("webpack");
const HtmlWebpackPlugin = require("html-webpack-plugin");
const HtmlInlineScriptPlugin = require("html-inline-script-webpack-plugin");

const viewerNextSrc = path.resolve(__dirname, "..", "viewer-next", "src");

function readEnvFile(name) {
  const file = path.resolve(__dirname, name);
  if (!fs.existsSync(file)) return {};
  const out = {};
  for (const rawLine of fs.readFileSync(file, "utf8").split(/\r?\n/)) {
    const line = rawLine.trim();
    if (!line || line.startsWith("#")) continue;
    const eq = line.indexOf("=");
    if (eq <= 0) continue;
    const key = line.slice(0, eq).trim();
    let value = line.slice(eq + 1).trim();
    if ((value.startsWith('"') && value.endsWith('"')) || (value.startsWith("'") && value.endsWith("'"))) {
      value = value.slice(1, -1);
    }
    out[key] = value;
  }
  return out;
}

module.exports = (env, argv) => {
  const isDev = argv.mode !== "production";
  const dashedEnv = readEnvFile(".env-production");
  const homeHelpReadmeUrl =
    process.env.PM_HOME_HELP_README_URL ||
    process.env.VITE_PM_HOME_HELP_README_URL ||
    dashedEnv.PM_HOME_HELP_README_URL ||
    dashedEnv.VITE_PM_HOME_HELP_README_URL ||
    "/help/en/readme.md";

  return {
    entry: path.resolve(__dirname, "src", "embed.tsx"),
    output: {
      path: path.resolve(__dirname, "..", "..", "dist", "shared"),
      filename: "home.bundle.js",
      // dist/shared may hold other shared runtime assets — never wipe the whole folder.
      clean: false,
      publicPath: "",
    },
    resolve: {
      extensions: [".tsx", ".ts", ".jsx", ".js", ".json"],
      alias: {
        // Subpaths must come before the bare `react`/`react-dom` file aliases (see webpack #11909).
        "react/jsx-runtime": require.resolve("preact/compat/jsx-runtime"),
        "react/jsx-dev-runtime": require.resolve("preact/compat/jsx-dev-runtime"),
        "react-dom/client": require.resolve("preact/compat/client"),
        "react-dom": require.resolve("preact/compat"),
        react: require.resolve("preact/compat"),
        "preact/jsx-runtime": require.resolve("preact/jsx-runtime"),
        "preact/jsx-dev-runtime": require.resolve("preact/compat/jsx-dev-runtime"),
        "preact/compat/client": require.resolve("preact/compat/client"),
        "preact/compat": require.resolve("preact/compat"),
        "@viewer-next": viewerNextSrc,
        "@/viewers/markdown": path.resolve(viewerNextSrc, "viewers", "markdown"),
        "@/bridge": path.resolve(viewerNextSrc, "bridge"),
        "@pm/shared": path.resolve(__dirname, "..", "shared"),
        "@": path.resolve(__dirname, "src"),
      },
      conditionNames: ["import", "module", "browser", "default"],
    },
    module: {
      rules: [
        {
          test: /\.[jt]sx?$/,
          include: [path.resolve(__dirname, "src"), path.resolve(__dirname, "..", "shared"), path.resolve(viewerNextSrc, "viewers", "markdown"), path.resolve(viewerNextSrc, "bridge")],
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
        filename: "home.html",
        inject: "body",
        minify: !isDev && {
          collapseWhitespace: true,
          removeComments: true,
          minifyCSS: true,
          minifyJS: true,
        },
      }),
      new webpack.DefinePlugin({
        __PM_HOME_HELP_README_URL__: JSON.stringify(homeHelpReadmeUrl),
      }),
      ...(!isDev ? [new HtmlInlineScriptPlugin()] : []),
    ],
    // Keep the embedded production HTML small. Inline sourcemaps made `home.html`
    // expensive to parse on first WebView2 navigation.
    devtool: isDev ? "eval-source-map" : false,
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
