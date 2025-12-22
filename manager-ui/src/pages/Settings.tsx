import { useState } from 'react';
import {
  Settings as SettingsIcon,
  Server,
  Moon,
  Sun,
  Save,
  RotateCcw,
  ExternalLink,
} from 'lucide-react';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Separator } from '@/components/ui/separator';
import { Switch } from '@/components/ui/switch';
import { useConfigStore } from '@/stores/configStore';
import { toast } from 'sonner';
import type { SimulatorConfig } from '@/types';

export function Settings() {
  const { config, theme, setConfig, setTheme, resetConfig } = useConfigStore();
  const [localConfig, setLocalConfig] = useState<SimulatorConfig>(config);

  const handleSave = () => {
    setConfig(localConfig);
    toast.success('Settings saved');
  };

  const handleReset = () => {
    resetConfig();
    setLocalConfig({
      host: 'elanlinux',
      controlPort: 8000,
      alpacaPort: 8100,
      polygonPort: 8200,
      finnhubPort: 8300,
      wsPort: 8400,
    });
    toast.info('Settings reset to defaults');
  };

  const toggleTheme = () => {
    setTheme(theme === 'dark' ? 'light' : 'dark');
  };

  return (
    <div className="p-6 max-w-4xl mx-auto space-y-6">
      {/* Header */}
      <div>
        <h1 className="text-2xl font-bold">Settings</h1>
        <p className="text-muted-foreground">Configure BrokerSimulator Manager</p>
      </div>

      {/* Connection Settings */}
      <Card>
        <CardHeader>
          <CardTitle className="flex items-center gap-2">
            <Server className="h-5 w-5" />
            Connection Settings
          </CardTitle>
          <CardDescription>
            Configure the connection to BrokerSimulator services on elanlinux
          </CardDescription>
        </CardHeader>
        <CardContent className="space-y-4">
          <div className="grid grid-cols-2 gap-4">
            <div className="space-y-2">
              <Label htmlFor="host">Host</Label>
              <Input
                id="host"
                value={localConfig.host}
                onChange={(e) => setLocalConfig({ ...localConfig, host: e.target.value })}
                placeholder="elanlinux"
              />
              <p className="text-xs text-muted-foreground">
                Hostname or IP of the Linux server running BrokerSimulator
              </p>
            </div>
          </div>

          <Separator />

          <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
            <div className="space-y-2">
              <Label htmlFor="controlPort">Control Port</Label>
              <Input
                id="controlPort"
                type="number"
                value={localConfig.controlPort}
                onChange={(e) => setLocalConfig({ ...localConfig, controlPort: parseInt(e.target.value) })}
              />
            </div>
            <div className="space-y-2">
              <Label htmlFor="alpacaPort">Alpaca Port</Label>
              <Input
                id="alpacaPort"
                type="number"
                value={localConfig.alpacaPort}
                onChange={(e) => setLocalConfig({ ...localConfig, alpacaPort: parseInt(e.target.value) })}
              />
            </div>
            <div className="space-y-2">
              <Label htmlFor="polygonPort">Polygon Port</Label>
              <Input
                id="polygonPort"
                type="number"
                value={localConfig.polygonPort}
                onChange={(e) => setLocalConfig({ ...localConfig, polygonPort: parseInt(e.target.value) })}
              />
            </div>
            <div className="space-y-2">
              <Label htmlFor="finnhubPort">Finnhub Port</Label>
              <Input
                id="finnhubPort"
                type="number"
                value={localConfig.finnhubPort}
                onChange={(e) => setLocalConfig({ ...localConfig, finnhubPort: parseInt(e.target.value) })}
              />
            </div>
          </div>

          <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
            <div className="space-y-2">
              <Label htmlFor="wsPort">WebSocket Port</Label>
              <Input
                id="wsPort"
                type="number"
                value={localConfig.wsPort}
                onChange={(e) => setLocalConfig({ ...localConfig, wsPort: parseInt(e.target.value) })}
              />
            </div>
          </div>

          <div className="flex gap-2 pt-4">
            <Button onClick={handleSave}>
              <Save className="h-4 w-4 mr-2" />
              Save Changes
            </Button>
            <Button variant="outline" onClick={handleReset}>
              <RotateCcw className="h-4 w-4 mr-2" />
              Reset to Defaults
            </Button>
          </div>
        </CardContent>
      </Card>

      {/* Appearance */}
      <Card>
        <CardHeader>
          <CardTitle className="flex items-center gap-2">
            {theme === 'dark' ? <Moon className="h-5 w-5" /> : <Sun className="h-5 w-5" />}
            Appearance
          </CardTitle>
          <CardDescription>
            Customize the look and feel of the application
          </CardDescription>
        </CardHeader>
        <CardContent className="space-y-4">
          <div className="flex items-center justify-between">
            <div>
              <Label>Dark Mode</Label>
              <p className="text-sm text-muted-foreground">
                Toggle between light and dark themes
              </p>
            </div>
            <Switch checked={theme === 'dark'} onCheckedChange={toggleTheme} />
          </div>
        </CardContent>
      </Card>

      {/* Quick Links */}
      <Card>
        <CardHeader>
          <CardTitle className="flex items-center gap-2">
            <ExternalLink className="h-5 w-5" />
            Quick Links
          </CardTitle>
          <CardDescription>
            Direct access to simulator services
          </CardDescription>
        </CardHeader>
        <CardContent>
          <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
            <a
              href={`http://${localConfig.host}:${localConfig.controlPort}`}
              target="_blank"
              rel="noopener noreferrer"
            >
              <Button variant="outline" className="w-full">
                Control API
                <ExternalLink className="h-4 w-4 ml-2" />
              </Button>
            </a>
            <a
              href={`http://${localConfig.host}:${localConfig.alpacaPort}/v2/account`}
              target="_blank"
              rel="noopener noreferrer"
            >
              <Button variant="outline" className="w-full">
                Alpaca API
                <ExternalLink className="h-4 w-4 ml-2" />
              </Button>
            </a>
            <a
              href={`http://${localConfig.host}:${localConfig.polygonPort}`}
              target="_blank"
              rel="noopener noreferrer"
            >
              <Button variant="outline" className="w-full">
                Polygon API
                <ExternalLink className="h-4 w-4 ml-2" />
              </Button>
            </a>
            <a
              href={`http://${localConfig.host}:${localConfig.finnhubPort}`}
              target="_blank"
              rel="noopener noreferrer"
            >
              <Button variant="outline" className="w-full">
                Finnhub API
                <ExternalLink className="h-4 w-4 ml-2" />
              </Button>
            </a>
          </div>
        </CardContent>
      </Card>

      {/* About */}
      <Card>
        <CardHeader>
          <CardTitle>About</CardTitle>
        </CardHeader>
        <CardContent className="space-y-2 text-sm text-muted-foreground">
          <p><strong className="text-foreground">BrokerSimulator Manager</strong> v1.0.0</p>
          <p>
            A management UI for controlling BrokerSimulator backtest sessions,
            exploring APIs, and monitoring WebSocket streams.
          </p>
          <p>
            The simulator runs on <code className="text-foreground">elanlinux</code> and provides
            Alpaca, Polygon, and Finnhub API simulation for high-fidelity backtesting.
          </p>
        </CardContent>
      </Card>
    </div>
  );
}
