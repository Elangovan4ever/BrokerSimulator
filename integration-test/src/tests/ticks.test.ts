/**
 * Integration tests for Polygon Tick APIs
 * Tests: /v2/ticks/stocks/trades/{ticker}/{date}, /v2/ticks/stocks/nbbo/{ticker}/{date}
 */

import axios from 'axios';
import { polygonClient, simulatorClient, config, logTestResult } from './setup';
import { extractSchema, compareSchemas, formatComparisonResult } from '../utils/schema-compare';

describe('Polygon Tick APIs', () => {
  const symbol = config.testSymbols[0];
  const date = config.testStartDate;

  describe('GET /v2/ticks/stocks/trades/{ticker}/{date}', () => {
    it('should return matching schema for trades ticks', async () => {
      let polygonResponse;
      try {
        polygonResponse = await polygonClient.getTradesTicks(symbol, date, { limit: 100 });
      } catch (error) {
        if (axios.isAxiosError(error) && [403, 404].includes(error.response?.status || 0)) {
          console.log('  SKIP: Trades ticks not available from Polygon API plan');
          return;
        }
        throw error;
      }
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
      let polygonResponse;
      try {
        polygonResponse = await polygonClient.getQuotesTicks(symbol, date, { limit: 100 });
      } catch (error) {
        if (axios.isAxiosError(error) && [403, 404].includes(error.response?.status || 0)) {
          console.log('  SKIP: NBBO ticks not available from Polygon API plan');
          return;
        }
        throw error;
      }
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
