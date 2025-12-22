/// <reference types="vite/client" />

interface ImportMetaEnv {
  readonly VITE_SIMULATOR_HOST: string;
  readonly VITE_CONTROL_PORT: string;
  readonly VITE_ALPACA_PORT: string;
  readonly VITE_POLYGON_PORT: string;
  readonly VITE_FINNHUB_PORT: string;
  readonly VITE_WS_PORT: string;
}

interface ImportMeta {
  readonly env: ImportMetaEnv;
}
