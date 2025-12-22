import { create } from 'zustand';
import { persist } from 'zustand/middleware';
import type { SimulatorConfig } from '@/types';

interface ConfigState {
  config: SimulatorConfig;
  theme: 'light' | 'dark';

  // Actions
  setConfig: (config: Partial<SimulatorConfig>) => void;
  setTheme: (theme: 'light' | 'dark') => void;
  resetConfig: () => void;
}

const defaultConfig: SimulatorConfig = {
  host: 'elanlinux',
  controlPort: 8000,
  alpacaPort: 8100,
  polygonPort: 8200,
  finnhubPort: 8300,
  wsPort: 8400,
};

export const useConfigStore = create<ConfigState>()(
  persist(
    (set) => ({
      config: defaultConfig,
      theme: 'dark',

      setConfig: (newConfig) => {
        set((state) => ({
          config: { ...state.config, ...newConfig },
        }));
      },

      setTheme: (theme) => {
        set({ theme });
        // Apply to document
        if (theme === 'dark') {
          document.documentElement.classList.add('dark');
        } else {
          document.documentElement.classList.remove('dark');
        }
      },

      resetConfig: () => {
        set({ config: defaultConfig });
      },
    }),
    {
      name: 'broker-simulator-config',
      partialize: (state) => ({
        config: state.config,
        theme: state.theme,
      }),
    }
  )
);
