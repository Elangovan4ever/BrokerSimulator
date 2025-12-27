/**
 * Integration tests for Polygon Ticker Events API
 * Tests: /vX/reference/tickers/{ticker}/events
 */

import { polygonClient, simulatorClient, logTestResult } from './setup';
import { extractSchema, compareSchemas, formatComparisonResult } from '../utils/schema-compare';

function getEventsCount(data: unknown): number {
  const results = (data as { results?: { events?: unknown } }).results;
  const events = results?.events;
  if (!Array.isArray(events)) {
    throw new Error('Ticker events response missing results.events array');
  }
  return events.length;
}

describe('Polygon Ticker Events API', () => {
  describe('GET /vX/reference/tickers/{ticker}/events', () => {
    it('should return matching schema for AAPL events', async () => {
      const polygonResponse = await polygonClient.getTickerEvents('AAPL', { limit: 10 });
      const simulatorResponse = await simulatorClient.getTickerEvents('AAPL', { limit: 10 });

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      logTestResult(
        'Ticker events AAPL',
        polygonResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      if (!comparison.match) {
        console.log(formatComparisonResult(comparison));
      }

      expect(polygonResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
      expect(getEventsCount(polygonResponse.data)).toBeGreaterThan(0);
      expect(getEventsCount(simulatorResponse.data)).toBeGreaterThan(0);
    });
  });
});
