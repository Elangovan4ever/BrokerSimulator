import React, { useState } from 'react';
import { ChevronRight, ChevronDown, Copy, Check } from 'lucide-react';
import { cn } from '@/lib/utils';
import { Button } from '@/components/ui/button';

interface JsonViewerProps {
  data: unknown;
  initialExpanded?: boolean;
  maxHeight?: string;
}

export function JsonViewer({ data, initialExpanded = true, maxHeight = '400px' }: JsonViewerProps) {
  const [copied, setCopied] = useState(false);

  const copyToClipboard = () => {
    navigator.clipboard.writeText(JSON.stringify(data, null, 2));
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  return (
    <div className="relative">
      <Button
        variant="ghost"
        size="icon"
        className="absolute top-2 right-2 h-7 w-7 z-10"
        onClick={copyToClipboard}
      >
        {copied ? (
          <Check className="h-4 w-4 text-success" />
        ) : (
          <Copy className="h-4 w-4" />
        )}
      </Button>
      <div
        className="bg-muted/50 rounded-lg p-4 font-mono text-sm overflow-auto custom-scrollbar"
        style={{ maxHeight }}
      >
        <JsonNode data={data} initialExpanded={initialExpanded} depth={0} />
      </div>
    </div>
  );
}

interface JsonNodeProps {
  data: unknown;
  initialExpanded: boolean;
  depth: number;
  keyName?: string;
}

function JsonNode({ data, initialExpanded, depth, keyName }: JsonNodeProps) {
  const [expanded, setExpanded] = useState(initialExpanded && depth < 2);

  if (data === null) {
    return (
      <span>
        {keyName && <span className="text-purple-400">"{keyName}"</span>}
        {keyName && ': '}
        <span className="text-orange-400">null</span>
      </span>
    );
  }

  if (typeof data === 'boolean') {
    return (
      <span>
        {keyName && <span className="text-purple-400">"{keyName}"</span>}
        {keyName && ': '}
        <span className="text-orange-400">{data.toString()}</span>
      </span>
    );
  }

  if (typeof data === 'number') {
    return (
      <span>
        {keyName && <span className="text-purple-400">"{keyName}"</span>}
        {keyName && ': '}
        <span className="text-blue-400">{data}</span>
      </span>
    );
  }

  if (typeof data === 'string') {
    return (
      <span>
        {keyName && <span className="text-purple-400">"{keyName}"</span>}
        {keyName && ': '}
        <span className="text-green-400">"{data}"</span>
      </span>
    );
  }

  if (Array.isArray(data)) {
    if (data.length === 0) {
      return (
        <span>
          {keyName && <span className="text-purple-400">"{keyName}"</span>}
          {keyName && ': '}
          <span className="text-muted-foreground">[]</span>
        </span>
      );
    }

    return (
      <div>
        <span
          className="cursor-pointer inline-flex items-center"
          onClick={() => setExpanded(!expanded)}
        >
          {expanded ? (
            <ChevronDown className="h-4 w-4 text-muted-foreground" />
          ) : (
            <ChevronRight className="h-4 w-4 text-muted-foreground" />
          )}
          {keyName && <span className="text-purple-400">"{keyName}"</span>}
          {keyName && ': '}
          <span className="text-muted-foreground">
            [{data.length} items]
          </span>
        </span>
        {expanded && (
          <div className="ml-4 border-l border-border pl-2">
            {data.map((item, index) => (
              <div key={index}>
                <JsonNode
                  data={item}
                  initialExpanded={initialExpanded}
                  depth={depth + 1}
                  keyName={String(index)}
                />
                {index < data.length - 1 && ','}
              </div>
            ))}
          </div>
        )}
      </div>
    );
  }

  if (typeof data === 'object') {
    const entries = Object.entries(data as Record<string, unknown>);

    if (entries.length === 0) {
      return (
        <span>
          {keyName && <span className="text-purple-400">"{keyName}"</span>}
          {keyName && ': '}
          <span className="text-muted-foreground">{'{}'}</span>
        </span>
      );
    }

    return (
      <div>
        <span
          className="cursor-pointer inline-flex items-center"
          onClick={() => setExpanded(!expanded)}
        >
          {expanded ? (
            <ChevronDown className="h-4 w-4 text-muted-foreground" />
          ) : (
            <ChevronRight className="h-4 w-4 text-muted-foreground" />
          )}
          {keyName && <span className="text-purple-400">"{keyName}"</span>}
          {keyName && ': '}
          <span className="text-muted-foreground">
            {'{'}
            {!expanded && `${entries.length} keys`}
            {!expanded && '}'}
          </span>
        </span>
        {expanded && (
          <div className="ml-4 border-l border-border pl-2">
            {entries.map(([key, value], index) => (
              <div key={key}>
                <JsonNode
                  data={value}
                  initialExpanded={initialExpanded}
                  depth={depth + 1}
                  keyName={key}
                />
                {index < entries.length - 1 && ','}
              </div>
            ))}
          </div>
        )}
        {expanded && <span className="text-muted-foreground">{'}'}</span>}
      </div>
    );
  }

  return <span className="text-muted-foreground">{String(data)}</span>;
}
