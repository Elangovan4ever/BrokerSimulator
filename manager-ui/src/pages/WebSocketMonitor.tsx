import { useState, useEffect, useRef } from 'react';
import {
  Radio,
  Plus,
  X,
  Send,
  Trash2,
  Pause,
  Play,
  Filter,
  Download,
} from 'lucide-react';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { Button } from '@/components/ui/button';
import { Badge } from '@/components/ui/badge';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Textarea } from '@/components/ui/textarea';
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@/components/ui/select';
import { ScrollArea } from '@/components/ui/scroll-area';
import { Separator } from '@/components/ui/separator';
import { Switch } from '@/components/ui/switch';
import { StatusIndicator } from '@/components/common/StatusIndicator';
import { JsonViewer } from '@/components/common/JsonViewer';
import { useWebSocketStore } from '@/stores/websocketStore';
import { getWsUrl } from '@/api/config';
import { formatTimestamp, generateId } from '@/lib/utils';
import type { ApiService, WebSocketConnection, WebSocketMessage } from '@/types';
import { toast } from 'sonner';

export function WebSocketMonitor() {
  const {
    connections,
    messages,
    connect,
    disconnect,
    send,
    subscribe,
    authenticate,
    clearMessages,
  } = useWebSocketStore();

  const [selectedService, setSelectedService] = useState<ApiService>('alpaca');
  const [customUrl, setCustomUrl] = useState('');
  const [useCustomUrl, setUseCustomUrl] = useState(false);
  const [selectedConnectionId, setSelectedConnectionId] = useState<string | null>(null);
  const [messageInput, setMessageInput] = useState('');
  const [subscriptionInput, setSubscriptionInput] = useState('');
  const [apiKey, setApiKey] = useState('');
  const [isPaused, setIsPaused] = useState(false);
  const [filterText, setFilterText] = useState('');
  const messagesEndRef = useRef<HTMLDivElement>(null);

  const selectedConnection = connections.find(c => c.id === selectedConnectionId);

  // Auto-scroll to bottom
  useEffect(() => {
    if (!isPaused && messagesEndRef.current) {
      messagesEndRef.current.scrollIntoView({ behavior: 'smooth' });
    }
  }, [messages, isPaused]);

  const handleConnect = () => {
    const url = useCustomUrl ? customUrl : getWsUrl(selectedService);
    const connectionId = connect(selectedService, useCustomUrl ? customUrl : undefined);
    setSelectedConnectionId(connectionId);
    toast.success(`Connecting to ${selectedService}...`);
  };

  const handleDisconnect = (connectionId: string) => {
    disconnect(connectionId);
    if (selectedConnectionId === connectionId) {
      setSelectedConnectionId(null);
    }
    toast.info('Disconnected');
  };

  const handleSendMessage = () => {
    if (!selectedConnectionId || !messageInput.trim()) return;

    try {
      const data = JSON.parse(messageInput);
      send(selectedConnectionId, data);
      setMessageInput('');
      toast.success('Message sent');
    } catch {
      toast.error('Invalid JSON');
    }
  };

  const handleSubscribe = () => {
    if (!selectedConnectionId || !subscriptionInput.trim()) return;

    const channels = subscriptionInput.split(',').map(s => s.trim());
    subscribe(selectedConnectionId, channels);
    setSubscriptionInput('');
    toast.success(`Subscribed to ${channels.join(', ')}`);
  };

  const handleAuthenticate = () => {
    if (!selectedConnectionId || !apiKey.trim()) return;

    authenticate(selectedConnectionId, apiKey);
    toast.success('Authentication sent');
  };

  const handleExportMessages = () => {
    const data = JSON.stringify(
      messages.filter(m => !selectedConnectionId || m.connectionId === selectedConnectionId),
      null,
      2
    );
    const blob = new Blob([data], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `ws-messages-${new Date().toISOString()}.json`;
    a.click();
    URL.revokeObjectURL(url);
    toast.success('Messages exported');
  };

  const filteredMessages = messages.filter(m => {
    if (selectedConnectionId && m.connectionId !== selectedConnectionId) return false;
    if (filterText) {
      const text = JSON.stringify(m.data).toLowerCase();
      return text.includes(filterText.toLowerCase());
    }
    return true;
  });

  const getMessageTypeColor = (type: 'sent' | 'received') => {
    return type === 'sent' ? 'bg-blue-500/10 text-blue-400 border-blue-500/30' : 'bg-green-500/10 text-green-400 border-green-500/30';
  };

  return (
    <div className="p-6 h-full flex flex-col">
      {/* Header */}
      <div className="flex items-center justify-between mb-6">
        <div>
          <h1 className="text-2xl font-bold">WebSocket Monitor</h1>
          <p className="text-muted-foreground">Connect to and monitor WebSocket streams</p>
        </div>
        <div className="flex items-center gap-2">
          <Button variant="outline" onClick={handleExportMessages}>
            <Download className="h-4 w-4 mr-2" />
            Export
          </Button>
          <Button
            variant={isPaused ? 'default' : 'outline'}
            onClick={() => setIsPaused(!isPaused)}
          >
            {isPaused ? <Play className="h-4 w-4 mr-2" /> : <Pause className="h-4 w-4 mr-2" />}
            {isPaused ? 'Resume' : 'Pause'}
          </Button>
        </div>
      </div>

      <div className="flex-1 flex gap-6 min-h-0">
        {/* Connections Panel */}
        <Card className="w-80 flex flex-col">
          <CardHeader className="pb-4">
            <CardTitle className="text-lg">Connections</CardTitle>
          </CardHeader>
          <CardContent className="space-y-4">
            {/* New Connection */}
            <div className="space-y-3">
              <div className="flex items-center justify-between">
                <Label>Use Custom URL</Label>
                <Switch checked={useCustomUrl} onCheckedChange={setUseCustomUrl} />
              </div>

              {useCustomUrl ? (
                <Input
                  value={customUrl}
                  onChange={(e) => setCustomUrl(e.target.value)}
                  placeholder="ws://elanlinux:8100/stream"
                />
              ) : (
                <Select value={selectedService} onValueChange={(v) => setSelectedService(v as ApiService)}>
                  <SelectTrigger>
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="alpaca">Alpaca (Trade Updates)</SelectItem>
                    <SelectItem value="polygon">Polygon (Market Data)</SelectItem>
                    <SelectItem value="finnhub">Finnhub (Trades)</SelectItem>
                    <SelectItem value="control">Control (Events)</SelectItem>
                  </SelectContent>
                </Select>
              )}

              <Button className="w-full" onClick={handleConnect}>
                <Plus className="h-4 w-4 mr-2" />
                Connect
              </Button>
            </div>

            <Separator />

            {/* Active Connections */}
            <div className="space-y-2">
              <Label className="text-xs text-muted-foreground">Active Connections</Label>
              {connections.length === 0 ? (
                <p className="text-xs text-muted-foreground text-center py-4">
                  No active connections
                </p>
              ) : (
                connections.map((conn) => (
                  <div
                    key={conn.id}
                    className={`p-3 rounded-lg border cursor-pointer transition-colors ${
                      selectedConnectionId === conn.id
                        ? 'border-primary bg-primary/5'
                        : 'hover:bg-muted/50'
                    }`}
                    onClick={() => setSelectedConnectionId(conn.id)}
                  >
                    <div className="flex items-center justify-between">
                      <div className="flex items-center gap-2">
                        <StatusIndicator status={conn.status} />
                        <span className="font-medium capitalize text-sm">{conn.service}</span>
                      </div>
                      <Button
                        variant="ghost"
                        size="icon"
                        className="h-6 w-6"
                        onClick={(e) => {
                          e.stopPropagation();
                          handleDisconnect(conn.id);
                        }}
                      >
                        <X className="h-3 w-3" />
                      </Button>
                    </div>
                    <div className="mt-1 text-xs text-muted-foreground">
                      <p className="truncate">{conn.url}</p>
                      <p className="mt-1">{conn.messageCount} messages</p>
                    </div>
                    {conn.subscriptions.length > 0 && (
                      <div className="mt-2 flex flex-wrap gap-1">
                        {conn.subscriptions.slice(0, 3).map((sub) => (
                          <Badge key={sub} variant="secondary" className="text-[10px]">
                            {sub}
                          </Badge>
                        ))}
                        {conn.subscriptions.length > 3 && (
                          <Badge variant="secondary" className="text-[10px]">
                            +{conn.subscriptions.length - 3}
                          </Badge>
                        )}
                      </div>
                    )}
                  </div>
                ))
              )}
            </div>
          </CardContent>
        </Card>

        {/* Messages Panel */}
        <Card className="flex-1 flex flex-col">
          <CardHeader className="pb-4">
            <div className="flex items-center justify-between">
              <div>
                <CardTitle className="text-lg">Messages</CardTitle>
                <CardDescription>
                  {filteredMessages.length} messages
                  {filterText && ` (filtered)`}
                </CardDescription>
              </div>
              <div className="flex items-center gap-2">
                <div className="relative">
                  <Filter className="absolute left-3 top-1/2 -translate-y-1/2 h-4 w-4 text-muted-foreground" />
                  <Input
                    value={filterText}
                    onChange={(e) => setFilterText(e.target.value)}
                    placeholder="Filter messages..."
                    className="pl-9 w-48"
                  />
                </div>
                <Button
                  variant="ghost"
                  size="icon"
                  onClick={() => clearMessages(selectedConnectionId || undefined)}
                >
                  <Trash2 className="h-4 w-4" />
                </Button>
              </div>
            </div>
          </CardHeader>
          <CardContent className="flex-1 min-h-0">
            <ScrollArea className="h-full">
              <div className="space-y-2 pr-4">
                {filteredMessages.length === 0 ? (
                  <p className="text-center text-muted-foreground py-8">
                    No messages yet
                  </p>
                ) : (
                  filteredMessages.map((msg) => (
                    <div
                      key={msg.id}
                      className={`p-3 rounded-lg border ws-message ${getMessageTypeColor(msg.type)}`}
                    >
                      <div className="flex items-center justify-between mb-2">
                        <Badge variant="outline" className="text-[10px]">
                          {msg.type === 'sent' ? '↑ SENT' : '↓ RECEIVED'}
                        </Badge>
                        <span className="text-[10px] text-muted-foreground">
                          {new Date(msg.timestamp).toLocaleTimeString()}
                        </span>
                      </div>
                      <pre className="text-xs font-mono overflow-x-auto">
                        {typeof msg.data === 'string'
                          ? msg.data
                          : JSON.stringify(msg.data, null, 2)}
                      </pre>
                    </div>
                  ))
                )}
                <div ref={messagesEndRef} />
              </div>
            </ScrollArea>
          </CardContent>
        </Card>

        {/* Actions Panel */}
        <Card className="w-72 flex flex-col">
          <CardHeader className="pb-4">
            <CardTitle className="text-lg">Actions</CardTitle>
          </CardHeader>
          <CardContent className="space-y-6">
            {/* Authentication */}
            <div className="space-y-2">
              <Label>Authentication</Label>
              <Input
                value={apiKey}
                onChange={(e) => setApiKey(e.target.value)}
                placeholder="API Key"
                type="password"
              />
              <Button
                className="w-full"
                variant="outline"
                onClick={handleAuthenticate}
                disabled={!selectedConnectionId}
              >
                Authenticate
              </Button>
            </div>

            <Separator />

            {/* Subscriptions */}
            <div className="space-y-2">
              <Label>Subscribe</Label>
              <Input
                value={subscriptionInput}
                onChange={(e) => setSubscriptionInput(e.target.value)}
                placeholder="T.AAPL, Q.MSFT"
              />
              <Button
                className="w-full"
                variant="outline"
                onClick={handleSubscribe}
                disabled={!selectedConnectionId}
              >
                Subscribe
              </Button>
              <p className="text-xs text-muted-foreground">
                Alpaca: T.*, Q.*, B.* | Polygon: T.*, Q.*, AM.* | Finnhub: symbol
              </p>
            </div>

            <Separator />

            {/* Send Message */}
            <div className="space-y-2">
              <Label>Send Custom Message</Label>
              <Textarea
                value={messageInput}
                onChange={(e) => setMessageInput(e.target.value)}
                placeholder='{"action": "subscribe", ...}'
                className="font-mono text-xs min-h-[100px]"
              />
              <Button
                className="w-full"
                onClick={handleSendMessage}
                disabled={!selectedConnectionId}
              >
                <Send className="h-4 w-4 mr-2" />
                Send
              </Button>
            </div>

            {/* Quick Actions */}
            {selectedConnection && (
              <>
                <Separator />
                <div className="space-y-2">
                  <Label>Quick Subscribe</Label>
                  <div className="grid grid-cols-2 gap-2">
                    {['AAPL', 'MSFT', 'GOOGL', 'AMZN'].map((symbol) => (
                      <Button
                        key={symbol}
                        variant="outline"
                        size="sm"
                        onClick={() => {
                          const prefix = selectedConnection.service === 'finnhub' ? '' : 'T.';
                          subscribe(selectedConnection.id, [`${prefix}${symbol}`]);
                        }}
                      >
                        {symbol}
                      </Button>
                    ))}
                  </div>
                </div>
              </>
            )}
          </CardContent>
        </Card>
      </div>
    </div>
  );
}
