import { useState, useEffect, useRef } from 'react';
import {
  Plus,
  X,
  Send,
  Trash2,
  Pause,
  Play,
  Filter,
  Download,
  Link,
  Unlink,
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
import { useWebSocketStore } from '@/stores/websocketStore';
import { useSessionStore } from '@/stores/sessionStore';
import { getWsUrl } from '@/api/config';
import type { ApiService } from '@/types';
import { toast } from 'sonner';

export function WebSocketMonitor() {
  const {
    connections,
    messages,
    connect,
    disconnect,
    send,
    subscribe,
    linkSession,
    unlinkSession,
    clearMessages,
  } = useWebSocketStore();

  const { sessions, fetchSessions } = useSessionStore();

  const [selectedService, setSelectedService] = useState<ApiService>('alpaca');
  const [customUrl, setCustomUrl] = useState('');
  const [useCustomUrl, setUseCustomUrl] = useState(false);
  const [selectedConnectionId, setSelectedConnectionId] = useState<string | null>(null);
  const [selectedSessionId, setSelectedSessionId] = useState<string>('');
  const [messageInput, setMessageInput] = useState('');
  const [subscriptionInput, setSubscriptionInput] = useState('');
  const [isPaused, setIsPaused] = useState(false);
  const [filterText, setFilterText] = useState('');
  const messagesEndRef = useRef<HTMLDivElement>(null);

  // Fetch sessions on mount
  useEffect(() => {
    fetchSessions();
  }, [fetchSessions]);

  // Auto-select first running session
  useEffect(() => {
    if (!selectedSessionId && sessions.length > 0) {
      const runningSession = sessions.find(s => s.status === 'RUNNING');
      if (runningSession) {
        setSelectedSessionId(runningSession.id);
      } else if (sessions.length > 0) {
        setSelectedSessionId(sessions[0].id);
      }
    }
  }, [sessions, selectedSessionId]);

  const selectedSession = sessions.find(s => s.id === selectedSessionId);

  const selectedConnection = connections.find(c => c.id === selectedConnectionId);

  // Auto-scroll to bottom
  useEffect(() => {
    if (!isPaused && messagesEndRef.current) {
      messagesEndRef.current.scrollIntoView({ behavior: 'smooth' });
    }
  }, [messages, isPaused]);

  const handleConnect = () => {
    let url = useCustomUrl ? customUrl : getWsUrl(selectedService);
    // Append session_id to URL if a session is selected
    if (selectedSessionId && !useCustomUrl) {
      url = `${url}?session_id=${selectedSessionId}`;
    }
    const connectionId = connect(selectedService, url);
    setSelectedConnectionId(connectionId);
    toast.success(`Connecting to ${selectedService}${selectedSessionId ? ' (linked to session)' : ''}...`);
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

  const handleLinkSession = () => {
    if (!selectedConnectionId || !selectedSessionId) return;

    // Use the proper linkSession method
    linkSession(selectedConnectionId, selectedSessionId);
    toast.success('Linking to session...');
  };

  const handleUnlinkSession = () => {
    if (!selectedConnectionId) return;

    unlinkSession(selectedConnectionId);
    toast.success('Unlinked from session');
  };

  // Check if selected connection is linked to a session
  const isLinked = selectedConnection?.linkedSessionId != null;

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
                      {conn.linkedSessionId && (
                        <p className="mt-1 text-green-500 flex items-center gap-1">
                          <Link className="h-3 w-3" />
                          Linked: {conn.linkedSessionId.slice(0, 8)}...
                        </p>
                      )}
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
            {/* Session Selection */}
            <div className="space-y-2">
              <Label>Link to Session</Label>
              <Select value={selectedSessionId} onValueChange={setSelectedSessionId}>
                <SelectTrigger>
                  <SelectValue placeholder="Select a session" />
                </SelectTrigger>
                <SelectContent>
                  {sessions.length === 0 ? (
                    <SelectItem value="" disabled>No sessions available</SelectItem>
                  ) : (
                    sessions.map((session) => (
                      <SelectItem key={session.id} value={session.id}>
                        {session.symbols?.join(', ') || 'No symbols'} ({session.status})
                      </SelectItem>
                    ))
                  )}
                </SelectContent>
              </Select>
              {selectedSession && (
                <div className="text-xs text-muted-foreground">
                  <p>Symbols: {selectedSession.symbols?.join(', ')}</p>
                  <p>Status: {selectedSession.status}</p>
                </div>
              )}
              {isLinked && (
                <div className="mt-2 p-2 rounded bg-green-500/10 border border-green-500/30">
                  <p className="text-xs text-green-500 flex items-center gap-1">
                    <Link className="h-3 w-3" />
                    Connected to session: {selectedConnection?.linkedSessionId?.slice(0, 8)}...
                  </p>
                </div>
              )}
              {selectedConnectionId && (
                isLinked ? (
                  <Button
                    className="w-full"
                    variant="destructive"
                    onClick={handleUnlinkSession}
                  >
                    <Unlink className="h-4 w-4 mr-2" />
                    Unlink from Session
                  </Button>
                ) : (
                  <Button
                    className="w-full"
                    variant="outline"
                    onClick={handleLinkSession}
                    disabled={!selectedSessionId}
                  >
                    <Link className="h-4 w-4 mr-2" />
                    Link Connection to Session
                  </Button>
                )
              )}
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

            {/* Quick Actions - Show session symbols */}
            {selectedConnection && selectedSession && selectedSession.symbols && selectedSession.symbols.length > 0 && (
              <>
                <Separator />
                <div className="space-y-2">
                  <Label>Session Symbols</Label>
                  <p className="text-xs text-muted-foreground">Subscribe to symbols from the linked session</p>
                  <div className="grid grid-cols-2 gap-2">
                    {selectedSession.symbols.map((symbol) => (
                      <Button
                        key={symbol}
                        variant="outline"
                        size="sm"
                        onClick={() => {
                          const prefix = selectedConnection.service === 'finnhub' ? '' : 'T.';
                          subscribe(selectedConnection.id, [`${prefix}${symbol}`]);
                          toast.success(`Subscribed to ${prefix}${symbol}`);
                        }}
                      >
                        {symbol}
                      </Button>
                    ))}
                  </div>
                  <div className="flex gap-2 mt-2">
                    <Button
                      variant="secondary"
                      size="sm"
                      className="flex-1"
                      onClick={() => {
                        const prefix = selectedConnection.service === 'finnhub' ? '' : 'T.';
                        const channels = selectedSession.symbols!.map(s => `${prefix}${s}`);
                        subscribe(selectedConnection.id, channels);
                        toast.success(`Subscribed to all trades`);
                      }}
                    >
                      All Trades
                    </Button>
                    <Button
                      variant="secondary"
                      size="sm"
                      className="flex-1"
                      onClick={() => {
                        const prefix = selectedConnection.service === 'finnhub' ? '' : 'Q.';
                        const channels = selectedSession.symbols!.map(s => `${prefix}${s}`);
                        subscribe(selectedConnection.id, channels);
                        toast.success(`Subscribed to all quotes`);
                      }}
                    >
                      All Quotes
                    </Button>
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
