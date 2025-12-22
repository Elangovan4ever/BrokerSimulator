import { BrowserRouter, Routes, Route } from 'react-router-dom';
import { Layout } from '@/components/common/Layout';
import { Dashboard } from '@/pages/Dashboard';
import { Sessions } from '@/pages/Sessions';
import { ApiExplorer } from '@/pages/ApiExplorer';
import { WebSocketMonitor } from '@/pages/WebSocketMonitor';
import { Settings } from '@/pages/Settings';
import { useConfigStore } from '@/stores/configStore';
import { useEffect } from 'react';

function App() {
  const { theme } = useConfigStore();

  // Apply theme on mount and changes
  useEffect(() => {
    if (theme === 'dark') {
      document.documentElement.classList.add('dark');
    } else {
      document.documentElement.classList.remove('dark');
    }
  }, [theme]);

  return (
    <BrowserRouter>
      <Layout>
        <Routes>
          <Route path="/" element={<Dashboard />} />
          <Route path="/sessions" element={<Sessions />} />
          <Route path="/explorer" element={<ApiExplorer />} />
          <Route path="/websocket" element={<WebSocketMonitor />} />
          <Route path="/settings" element={<Settings />} />
        </Routes>
      </Layout>
    </BrowserRouter>
  );
}

export default App;
