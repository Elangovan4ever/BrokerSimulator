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

// ============ Timezone Utilities ============
// All times in UI are displayed/input in Eastern Time (ET)
// Backend stores/processes times in UTC

const ET_TIMEZONE = 'America/New_York';

/**
 * Convert a datetime-local string (in ET) to UTC ISO string.
 * Input: "2025-12-26T04:00:00" (4AM ET)
 * Output: "2025-12-26T09:00:00" (9AM UTC)
 */
export function etToUtc(etDatetimeLocal: string): string {
  // Parse the datetime-local input
  const [datePart, timePart] = etDatetimeLocal.split('T');
  const [year, month, day] = datePart.split('-').map(Number);
  const timeParts = timePart.split(':');
  const hour = parseInt(timeParts[0]);
  const minute = parseInt(timeParts[1]);
  const second = timeParts[2] ? parseInt(timeParts[2]) : 0;

  // Create a formatter to find what UTC hour corresponds to this ET hour
  // We'll use a reference point and calculate the offset
  const referenceUtc = new Date(Date.UTC(year, month - 1, day, 12, 0, 0));
  const etFormatter = new Intl.DateTimeFormat('en-US', {
    timeZone: ET_TIMEZONE,
    hour: 'numeric',
    hour12: false,
  });
  const etHourAt12Utc = parseInt(etFormatter.format(referenceUtc));
  // Offset: how many hours to ADD to ET to get UTC
  // If 12 UTC = 7 ET, then offset = 12 - 7 = 5 (ET + 5 = UTC)
  const offsetHours = 12 - etHourAt12Utc;

  // Convert ET time to UTC by adding the offset
  const utcDate = new Date(Date.UTC(year, month - 1, day, hour + offsetHours, minute, second));

  const utcYear = utcDate.getUTCFullYear();
  const utcMonth = String(utcDate.getUTCMonth() + 1).padStart(2, '0');
  const utcDay = String(utcDate.getUTCDate()).padStart(2, '0');
  const utcHours = String(utcDate.getUTCHours()).padStart(2, '0');
  const utcMinutes = String(utcDate.getUTCMinutes()).padStart(2, '0');
  const utcSeconds = String(utcDate.getUTCSeconds()).padStart(2, '0');

  return `${utcYear}-${utcMonth}-${utcDay}T${utcHours}:${utcMinutes}:${utcSeconds}`;
}

/**
 * Convert a UTC ISO string to ET datetime-local string
 */
export function utcToEt(utcIsoString: string): string {
  const utcDate = new Date(utcIsoString + (utcIsoString.endsWith('Z') ? '' : 'Z'));

  const etFormatter = new Intl.DateTimeFormat('en-US', {
    timeZone: ET_TIMEZONE,
    year: 'numeric',
    month: '2-digit',
    day: '2-digit',
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    hour12: false,
  });

  const parts = etFormatter.formatToParts(utcDate);
  const year = parts.find(p => p.type === 'year')?.value;
  const month = parts.find(p => p.type === 'month')?.value;
  const day = parts.find(p => p.type === 'day')?.value;
  const hour = parts.find(p => p.type === 'hour')?.value;
  const minute = parts.find(p => p.type === 'minute')?.value;
  const second = parts.find(p => p.type === 'second')?.value;

  return `${year}-${month}-${day}T${hour}:${minute}:${second}`;
}

/**
 * Format a UTC timestamp for display in ET
 */
export function formatTimestampET(utcTimestamp: string): string {
  if (!utcTimestamp) return '-';
  const utcDate = new Date(utcTimestamp + (utcTimestamp.endsWith('Z') ? '' : 'Z'));
  return utcDate.toLocaleString('en-US', { timeZone: ET_TIMEZONE }) + ' ET';
}

/**
 * Get current date in ET as YYYY-MM-DD
 */
export function getCurrentDateET(): string {
  const now = new Date();
  const etFormatter = new Intl.DateTimeFormat('en-US', {
    timeZone: ET_TIMEZONE,
    year: 'numeric',
    month: '2-digit',
    day: '2-digit',
  });
  const parts = etFormatter.formatToParts(now);
  const year = parts.find(p => p.type === 'year')?.value;
  const month = parts.find(p => p.type === 'month')?.value;
  const day = parts.find(p => p.type === 'day')?.value;
  return `${year}-${month}-${day}`;
}

/**
 * Get default session start time (4:00 AM ET today)
 */
export function getDefaultStartTimeET(): string {
  return `${getCurrentDateET()}T04:00:00`;
}

/**
 * Get default session end time (8:00 PM ET today)
 */
export function getDefaultEndTimeET(): string {
  return `${getCurrentDateET()}T20:00:00`;
}
