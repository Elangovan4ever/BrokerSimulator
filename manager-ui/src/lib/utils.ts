import { type ClassValue, clsx } from 'clsx';
import { twMerge } from 'tailwind-merge';

export function cn(...inputs: ClassValue[]) {
  return twMerge(clsx(inputs));
}

export function formatCurrency(value: number): string {
  return new Intl.NumberFormat('en-US', {
    style: 'currency',
    currency: 'USD',
    minimumFractionDigits: 2,
    maximumFractionDigits: 2,
  }).format(value);
}

export function formatNumber(value: number, decimals: number = 2): string {
  return new Intl.NumberFormat('en-US', {
    minimumFractionDigits: decimals,
    maximumFractionDigits: decimals,
  }).format(value);
}

export function formatPercent(value: number): string {
  const sign = value >= 0 ? '+' : '';
  return `${sign}${(value * 100).toFixed(2)}%`;
}

export function formatTimestamp(timestamp: string): string {
  return new Date(timestamp).toLocaleString();
}

export function formatDuration(ms: number): string {
  if (ms < 1000) return `${ms}ms`;
  if (ms < 60000) return `${(ms / 1000).toFixed(2)}s`;
  return `${(ms / 60000).toFixed(2)}m`;
}

export function generateId(): string {
  return Math.random().toString(36).substring(2, 11);
}

export function getStatusColor(status: string): string {
  switch (status.toLowerCase()) {
    case 'running':
    case 'online':
    case 'connected':
    case 'filled':
    case 'success':
      return 'text-success';
    case 'paused':
    case 'pending':
    case 'partially_filled':
      return 'text-warning';
    case 'stopped':
    case 'offline':
    case 'disconnected':
    case 'canceled':
      return 'text-muted-foreground';
    case 'error':
    case 'rejected':
    case 'failed':
      return 'text-destructive';
    default:
      return 'text-foreground';
  }
}

export function getStatusBgColor(status: string): string {
  switch (status.toLowerCase()) {
    case 'running':
    case 'online':
    case 'connected':
      return 'bg-success/10 text-success';
    case 'paused':
    case 'pending':
      return 'bg-warning/10 text-warning';
    case 'stopped':
    case 'offline':
      return 'bg-muted text-muted-foreground';
    case 'error':
      return 'bg-destructive/10 text-destructive';
    default:
      return 'bg-secondary text-secondary-foreground';
  }
}

export function truncateMiddle(str: string, maxLength: number = 20): string {
  if (str.length <= maxLength) return str;
  const half = Math.floor((maxLength - 3) / 2);
  return `${str.slice(0, half)}...${str.slice(-half)}`;
}
