/** Used by webpack only (`npm run build:embed`). Vite uses `@tailwindcss/vite` in `vite.config.ts`. */
module.exports = {
  plugins: {
    "@tailwindcss/postcss": {},
  },
};
