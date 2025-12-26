import { useState, useEffect } from 'react';
import {
  Send,
  Clock,
  Trash2,
  Server,
  Activity,
  AlertCircle,
} from 'lucide-react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Textarea } from '@/components/ui/textarea';
import { Tabs, TabsContent, TabsList, TabsTrigger } from '@/components/ui/tabs';
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@/components/ui/select';
import { ScrollArea } from '@/components/ui/scroll-area';
import { Separator } from '@/components/ui/separator';
import { JsonViewer } from '@/components/common/JsonViewer';
import { apiEndpoints } from '@/api/endpoints';
import { makeRequest } from '@/api/client';
import { useSessionStore } from '@/stores/sessionStore';
import { formatDuration } from '@/lib/utils';
import type { ApiService, HttpMethod, ApiEndpoint, ApiResponse } from '@/types';
import { toast } from 'sonner';

interface RequestHistory {
  id: string;
  service: ApiService;
  method: HttpMethod;
  path: string;
  response: ApiResponse;
}

type ApiMode = 'session' | 'broker';
type BrokerService = 'alpaca' | 'polygon' | 'finnhub';

export function ApiExplorer() {
  const [apiMode, setApiMode] = useState<ApiMode>('session');
  const [brokerService, setBrokerService] = useState<BrokerService>('polygon');
  const [selectedEndpoint, setSelectedEndpoint] = useState<ApiEndpoint | null>(null);
  const [method, setMethod] = useState<HttpMethod>('GET');
  const [path, setPath] = useState('');
  const [pathParams, setPathParams] = useState<Record<string, string>>({});
  const [queryParams, setQueryParams] = useState<Record<string, string>>({});
  const [body, setBody] = useState('');
  const [response, setResponse] = useState<ApiResponse | null>(null);
  const [isLoading, setIsLoading] = useState(false);
  const [history, setHistory] = useState<RequestHistory[]>([]);
  const [selectedSessionId, setSelectedSessionId] = useState<string>('');

  const { sessions, fetchSessions } = useSessionStore();

  // Fetch sessions on mount (live updates come from status WebSocket)
  useEffect(() => {
    fetchSessions();
  }, [fetchSessions]);

  // Filter running sessions for broker APIs
  const runningSessions = sessions.filter(s => s.status === 'RUNNING');

  // Auto-select first running session when switching to broker mode
  useEffect(() => {
    if (apiMode === 'broker' && !selectedSessionId && runningSessions.length > 0) {
      setSelectedSessionId(runningSessions[0].id);
    }
  }, [apiMode, runningSessions, selectedSessionId]);

  // Clear selected endpoint when switching modes
  useEffect(() => {
    setSelectedEndpoint(null);
    setPath('');
    setResponse(null);
  }, [apiMode, brokerService]);

  // Get current service based on mode
  const currentService: ApiService = apiMode === 'session' ? 'control' : brokerService;
  const endpoints = apiEndpoints[currentService] || [];
  const selectedSession = sessions.find(s => s.id === selectedSessionId);

  const handleSelectEndpoint = (endpoint: ApiEndpoint) => {
    setSelectedEndpoint(endpoint);
    setMethod(endpoint.method);
    setPath(endpoint.path);
    setBody(endpoint.body ? JSON.stringify(endpoint.body, null, 2) : '');

    // Reset params
    const newPathParams: Record<string, string> = {};
    const newQueryParams: Record<string, string> = {};

    // Initialize params with defaults or session_id
    endpoint.params?.forEach(param => {
      if (param.name === 'session_id' && selectedSessionId) {
        if (param.type === 'path') {
          newPathParams[param.name] = selectedSessionId;
        } else if (param.type === 'query') {
          newQueryParams[param.name] = selectedSessionId;
        }
      } else if (param.default) {
        if (param.type === 'path') {
          newPathParams[param.name] = param.default;
        } else if (param.type === 'query') {
          newQueryParams[param.name] = param.default;
        }
      }
    });

    setPathParams(newPathParams);
    setQueryParams(newQueryParams);
  };

  const buildFinalPath = (): string => {
    let finalPath = path;
    Object.entries(pathParams).forEach(([key, value]) => {
      finalPath = finalPath.replace(`{${key}}`, value);
    });
    return finalPath;
  };

  const handleSend = async () => {
    // For broker APIs, require a running session
    if (apiMode === 'broker' && !selectedSessionId) {
      toast.error('Please select a running session first');
      return;
    }

    setIsLoading(true);
    try {
      const finalPath = buildFinalPath();
      const bodyData = body ? JSON.parse(body) : undefined;

      // For broker APIs, always include session_id in query params
      const finalQueryParams = { ...queryParams };
      if (apiMode === 'broker' && selectedSessionId) {
        finalQueryParams.session_id = selectedSessionId;
      }

      const result = await makeRequest(
        currentService,
        method,
        finalPath,
        bodyData,
        Object.keys(finalQueryParams).length > 0 ? finalQueryParams : undefined
      );

      setResponse(result);

      // Add to history
      const historyEntry: RequestHistory = {
        id: Date.now().toString(),
        service: currentService,
        method,
        path: finalPath,
        response: result,
      };
      setHistory(prev => [historyEntry, ...prev].slice(0, 50));

      if (result.status >= 200 && result.status < 300) {
        toast.success(`${method} ${finalPath} - ${result.status}`);
      } else {
        toast.error(`${method} ${finalPath} - ${result.status}`);
      }
    } catch (error) {
      toast.error(`Request failed: ${(error as Error).message}`);
    } finally {
      setIsLoading(false);
    }
  };

  const getMethodColor = (m: HttpMethod) => {
    switch (m) {
      case 'GET': return 'text-green-500';
      case 'POST': return 'text-blue-500';
      case 'PUT': return 'text-yellow-500';
      case 'PATCH': return 'text-orange-500';
      case 'DELETE': return 'text-red-500';
      default: return 'text-muted-foreground';
    }
  };

  const getStatusColor = (status: number) => {
    if (status >= 200 && status < 300) return 'text-success';
    if (status >= 400 && status < 500) return 'text-warning';
    if (status >= 500) return 'text-destructive';
    return 'text-muted-foreground';
  };

  // Render the endpoint list
  const renderEndpointList = () => (
    <ScrollArea className="h-full">
      <div className="p-4 space-y-1">
        {endpoints.map((endpoint, idx) => (
          <button
            key={idx}
            className={`w-full text-left p-2 rounded-md text-sm hover:bg-muted/50 transition-colors ${
              selectedEndpoint === endpoint ? 'bg-muted' : ''
            }`}
            onClick={() => handleSelectEndpoint(endpoint)}
          >
            <div className="flex items-center gap-2">
              <span className={`font-mono text-xs font-semibold ${getMethodColor(endpoint.method)}`}>
                {endpoint.method}
              </span>
              <span className="font-mono text-xs truncate">{endpoint.path}</span>
            </div>
            <p className="text-xs text-muted-foreground mt-1">{endpoint.description}</p>
          </button>
        ))}
      </div>
    </ScrollArea>
  );

  return (
    <div className="p-6 h-full flex flex-col">
      {/* Header */}
      <div className="mb-6">
        <h1 className="text-2xl font-bold">API Explorer</h1>
        <p className="text-muted-foreground">Test BrokerSimulator API endpoints</p>
      </div>

      <div className="flex-1 flex gap-6 min-h-0">
        {/* Endpoints Sidebar with Tabs */}
        <Card className="w-80 flex flex-col">
          <Tabs value={apiMode} onValueChange={(v) => setApiMode(v as ApiMode)} className="flex flex-col h-full">
            <CardHeader className="pb-2">
              <TabsList className="grid w-full grid-cols-2">
                <TabsTrigger value="session" className="flex items-center gap-1">
                  <Server className="h-3 w-3" />
                  Session APIs
                </TabsTrigger>
                <TabsTrigger value="broker" className="flex items-center gap-1">
                  <Activity className="h-3 w-3" />
                  Broker APIs
                </TabsTrigger>
              </TabsList>
            </CardHeader>

            <CardContent className="flex-1 p-0 flex flex-col min-h-0 overflow-hidden">
              {/* Session APIs Tab */}
              <TabsContent value="session" className="flex-1 m-0 flex flex-col min-h-0 data-[state=inactive]:hidden">
                <div className="px-4 py-2 border-b shrink-0">
                  <p className="text-xs text-muted-foreground">
                    Control API (Port 8000) - Manage simulation sessions
                  </p>
                </div>
                <div className="flex-1 min-h-0 overflow-hidden">
                  {renderEndpointList()}
                </div>
              </TabsContent>

              {/* Broker APIs Tab */}
              <TabsContent value="broker" className="flex-1 m-0 flex flex-col min-h-0 data-[state=inactive]:hidden">
                <div className="px-4 py-3 border-b space-y-3 shrink-0">
                  {/* Running Session Selector */}
                  <div className="space-y-2">
                    <Label className="text-xs font-medium flex items-center gap-1">
                      <Activity className="h-3 w-3 text-green-500" />
                      Running Session
                    </Label>
                    {runningSessions.length === 0 ? (
                      <div className="p-3 border border-destructive/50 bg-destructive/10 rounded-md space-y-2">
                        <div className="flex items-center gap-2 text-sm text-destructive font-medium">
                          <AlertCircle className="h-4 w-4" />
                          No running sessions
                        </div>
                        <p className="text-xs text-muted-foreground">
                          Go to Sessions page and start a session first. Broker APIs require an active session to return market data.
                        </p>
                      </div>
                    ) : (
                      <>
                        <Select value={selectedSessionId} onValueChange={setSelectedSessionId}>
                          <SelectTrigger>
                            <SelectValue placeholder="Select running session" />
                          </SelectTrigger>
                          <SelectContent>
                            {runningSessions.map((session) => (
                              <SelectItem key={session.id} value={session.id}>
                                {session.symbols?.slice(0, 2).join(', ')}{session.symbols && session.symbols.length > 2 ? '...' : ''}
                              </SelectItem>
                            ))}
                          </SelectContent>
                        </Select>
                        {selectedSession && (
                          <div className="text-xs text-muted-foreground p-2 bg-muted/50 rounded">
                            <p className="font-mono truncate">{selectedSessionId.slice(0, 12)}...</p>
                            <p>Symbols: {selectedSession.symbols?.join(', ')}</p>
                          </div>
                        )}
                      </>
                    )}
                  </div>

                  <Separator />

                  {/* Broker Service Selector */}
                  <div className="space-y-2">
                    <Label className="text-xs font-medium">Broker API</Label>
                    <Select value={brokerService} onValueChange={(v) => setBrokerService(v as BrokerService)}>
                      <SelectTrigger>
                        <SelectValue />
                      </SelectTrigger>
                      <SelectContent>
                        <SelectItem value="alpaca">Alpaca API (8100)</SelectItem>
                        <SelectItem value="polygon">Polygon API (8200)</SelectItem>
                        <SelectItem value="finnhub">Finnhub API (8300)</SelectItem>
                      </SelectContent>
                    </Select>
                  </div>
                </div>
                <div className="flex-1 min-h-0 overflow-hidden">
                  {renderEndpointList()}
                </div>
              </TabsContent>
            </CardContent>
          </Tabs>
        </Card>

        {/* Request Builder */}
        <div className="flex-1 flex flex-col gap-6">
          {/* Request Section */}
          <Card>
            <CardHeader className="pb-4">
              <CardTitle className="text-lg">Request</CardTitle>
            </CardHeader>
            <CardContent className="space-y-4">
              {/* Method & Path */}
              <div className="flex gap-2">
                <Select value={method} onValueChange={(v) => setMethod(v as HttpMethod)}>
                  <SelectTrigger className="w-28">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="GET">GET</SelectItem>
                    <SelectItem value="POST">POST</SelectItem>
                    <SelectItem value="PUT">PUT</SelectItem>
                    <SelectItem value="PATCH">PATCH</SelectItem>
                    <SelectItem value="DELETE">DELETE</SelectItem>
                  </SelectContent>
                </Select>
                <Input
                  value={path}
                  onChange={(e) => setPath(e.target.value)}
                  placeholder="/v2/account"
                  className="flex-1 font-mono"
                />
                <Button
                  onClick={handleSend}
                  disabled={isLoading || !path || (apiMode === 'broker' && !selectedSessionId)}
                >
                  <Send className="h-4 w-4 mr-2" />
                  Send
                </Button>
              </div>

              {/* Parameters */}
              {selectedEndpoint?.params && selectedEndpoint.params.length > 0 && (
                <div className="space-y-3">
                  <Label>Parameters</Label>
                  <div className="grid grid-cols-2 gap-3">
                    {selectedEndpoint.params.map((param) => (
                      <div key={param.name} className="space-y-1">
                        <Label className="text-xs flex items-center gap-1">
                          <span className={`px-1 rounded text-[10px] ${
                            param.type === 'path' ? 'bg-blue-500/20 text-blue-400' : 'bg-green-500/20 text-green-400'
                          }`}>
                            {param.type}
                          </span>
                          {param.name}
                          {param.required && <span className="text-destructive">*</span>}
                        </Label>
                        <Input
                          placeholder={param.description}
                          value={param.type === 'path' ? pathParams[param.name] || '' : queryParams[param.name] || ''}
                          onChange={(e) => {
                            if (param.type === 'path') {
                              setPathParams(prev => ({ ...prev, [param.name]: e.target.value }));
                            } else {
                              setQueryParams(prev => ({ ...prev, [param.name]: e.target.value }));
                            }
                          }}
                        />
                      </div>
                    ))}
                  </div>
                </div>
              )}

              {/* Body */}
              {['POST', 'PUT', 'PATCH'].includes(method) && (
                <div className="space-y-2">
                  <Label>Request Body (JSON)</Label>
                  <Textarea
                    value={body}
                    onChange={(e) => setBody(e.target.value)}
                    className="font-mono text-sm min-h-[120px]"
                    placeholder='{"key": "value"}'
                  />
                </div>
              )}
            </CardContent>
          </Card>

          {/* Response Section */}
          <Card className="flex-1 flex flex-col min-h-0">
            <CardHeader className="pb-4">
              <div className="flex items-center justify-between">
                <CardTitle className="text-lg">Response</CardTitle>
                {response && (
                  <div className="flex items-center gap-4 text-sm">
                    <span className={`font-semibold ${getStatusColor(response.status)}`}>
                      {response.status} {response.statusText}
                    </span>
                    <span className="text-muted-foreground flex items-center gap-1">
                      <Clock className="h-4 w-4" />
                      {formatDuration(response.duration)}
                    </span>
                  </div>
                )}
              </div>
            </CardHeader>
            <CardContent className="flex-1 min-h-0">
              {isLoading ? (
                <div className="h-full flex items-center justify-center text-muted-foreground">
                  Loading...
                </div>
              ) : response ? (
                <JsonViewer data={response.data} maxHeight="100%" />
              ) : (
                <div className="h-full flex items-center justify-center text-muted-foreground">
                  {apiMode === 'broker' && !selectedSessionId
                    ? 'Select a running session to test broker APIs'
                    : 'Send a request to see the response'
                  }
                </div>
              )}
            </CardContent>
          </Card>
        </div>

        {/* History Sidebar */}
        <Card className="w-64 flex flex-col">
          <CardHeader className="pb-2">
            <div className="flex items-center justify-between">
              <CardTitle className="text-sm">History</CardTitle>
              {history.length > 0 && (
                <Button
                  variant="ghost"
                  size="icon"
                  className="h-6 w-6"
                  onClick={() => setHistory([])}
                >
                  <Trash2 className="h-3 w-3" />
                </Button>
              )}
            </div>
          </CardHeader>
          <CardContent className="flex-1 p-0">
            <ScrollArea className="h-full">
              <div className="p-4 space-y-2">
                {history.length === 0 ? (
                  <p className="text-xs text-muted-foreground text-center py-4">
                    No requests yet
                  </p>
                ) : (
                  history.map((item) => (
                    <button
                      key={item.id}
                      className="w-full text-left p-2 rounded-md text-xs hover:bg-muted/50 transition-colors"
                      onClick={() => {
                        // Restore from history
                        if (item.service === 'control') {
                          setApiMode('session');
                        } else {
                          setApiMode('broker');
                          setBrokerService(item.service as BrokerService);
                        }
                        setMethod(item.method);
                        setPath(item.path);
                        setResponse(item.response);
                      }}
                    >
                      <div className="flex items-center gap-2">
                        <span className={`font-mono font-semibold ${getMethodColor(item.method)}`}>
                          {item.method}
                        </span>
                        <span className={`font-mono ${getStatusColor(item.response.status)}`}>
                          {item.response.status}
                        </span>
                      </div>
                      <p className="font-mono truncate text-muted-foreground mt-1">
                        {item.path}
                      </p>
                    </button>
                  ))
                )}
              </div>
            </ScrollArea>
          </CardContent>
        </Card>
      </div>
    </div>
  );
}
