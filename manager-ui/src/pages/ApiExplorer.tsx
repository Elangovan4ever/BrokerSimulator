import { useState, useEffect } from 'react';
import {
  Send,
  Clock,
  Trash2,
} from 'lucide-react';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Button } from '@/components/ui/button';
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

export function ApiExplorer() {
  const [selectedService, setSelectedService] = useState<ApiService>('control');
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

  // Fetch sessions on mount
  useEffect(() => {
    fetchSessions();
  }, [fetchSessions]);

  // Auto-select first session
  useEffect(() => {
    if (!selectedSessionId && sessions.length > 0) {
      setSelectedSessionId(sessions[0].id);
    }
  }, [sessions, selectedSessionId]);

  // Auto-populate session_id in path params when session changes
  useEffect(() => {
    if (selectedSessionId && selectedEndpoint?.params?.some(p => p.name === 'session_id')) {
      setPathParams(prev => ({ ...prev, session_id: selectedSessionId }));
    }
  }, [selectedSessionId, selectedEndpoint]);

  const endpoints = apiEndpoints[selectedService] || [];
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
        // Auto-fill session_id from selected session
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
    setIsLoading(true);
    try {
      const finalPath = buildFinalPath();
      const bodyData = body ? JSON.parse(body) : undefined;

      const result = await makeRequest(
        selectedService,
        method,
        finalPath,
        bodyData,
        Object.keys(queryParams).length > 0 ? queryParams : undefined
      );

      setResponse(result);

      // Add to history
      const historyEntry: RequestHistory = {
        id: Date.now().toString(),
        service: selectedService,
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

  return (
    <div className="p-6 h-full flex flex-col">
      {/* Header */}
      <div className="mb-6">
        <h1 className="text-2xl font-bold">API Explorer</h1>
        <p className="text-muted-foreground">Test BrokerSimulator API endpoints</p>
      </div>

      <div className="flex-1 flex gap-6 min-h-0">
        {/* Endpoints Sidebar */}
        <Card className="w-80 flex flex-col">
          <CardHeader className="pb-2 space-y-3">
            <Select value={selectedService} onValueChange={(v) => setSelectedService(v as ApiService)}>
              <SelectTrigger>
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                <SelectItem value="control">Control API (8000)</SelectItem>
                <SelectItem value="alpaca">Alpaca API (8100)</SelectItem>
                <SelectItem value="polygon">Polygon API (8200)</SelectItem>
                <SelectItem value="finnhub">Finnhub API (8300)</SelectItem>
              </SelectContent>
            </Select>

            <Separator />

            <div className="space-y-2">
              <Label className="text-xs text-muted-foreground">Active Session</Label>
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
                        {session.symbols?.slice(0, 2).join(', ')}{session.symbols && session.symbols.length > 2 ? '...' : ''} ({session.status})
                      </SelectItem>
                    ))
                  )}
                </SelectContent>
              </Select>
              {selectedSession && (
                <div className="text-xs text-muted-foreground p-2 bg-muted/50 rounded">
                  <p className="font-mono truncate">{selectedSessionId.slice(0, 12)}...</p>
                  <p>Status: <span className={selectedSession.status === 'RUNNING' ? 'text-green-500' : ''}>{selectedSession.status}</span></p>
                </div>
              )}
            </div>
          </CardHeader>
          <CardContent className="flex-1 p-0">
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
          </CardContent>
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
                <Button onClick={handleSend} disabled={isLoading || !path}>
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
                  Send a request to see the response
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
                        setSelectedService(item.service);
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
