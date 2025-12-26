/**
 * Integration tests for Polygon Tick APIs
 * Tests: /v2/ticks/stocks/trades/{ticker}/{date}, /v2/ticks/stocks/nbbo/{ticker}/{date}
 */

import { polygonClient, simulatorClient, config, logTestResult } from './setup';
import { extractSchema, compareSchemas, formatComparisonResult } from '../utils/schema-compare';

describe('Polygon Tick APIs', () => {
  const symbol = config.testSymbols[0];
  const date = config.testStartDate;

  describe('GET /v2/ticks/stocks/trades/{ticker}/{date}', () => {
    it('should return matching schema for trades ticks', async () => {
      const polygonResponse = await polygonClient.getTradesTicks(symbol, date, { limit: 100 });
      const simulatorResponse = await simulatorClient.getTradesTicks(symbol, date, { limit: 100 });

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      logTestResult(
        'Trades ticks schema',
        polygonResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      if (!comparison.match) {
        console.log(formatComparisonResult(comparison));
      }

      expect(polygonResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
    });
  });

  describe('GET /v2/ticks/stocks/nbbo/{ticker}/{date}', () => {
    it('should return matching schema for NBBO ticks', async () => {
      const polygonResponse = await polygonClient.getQuotesTicks(symbol, date, { limit: 100 });
      const simulatorResponse = await simulatorClient.getQuotesTicks(symbol, date, { limit: 100 });

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      logTestResult(
        'NBBO ticks schema',
        polygonResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      if (!comparison.match) {
        console.log(formatComparisonResult(comparison));
      }

      expect(polygonResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
    });
  });
});
