/**
 * Integration tests for Polygon Trades API
 * Tests: /v3/trades/{symbol}, /v2/ticks/stocks/trades/{symbol}/{date}, /v2/last/trade/{symbol}
 */

import { polygonClient, simulatorClient, config, logTestResult } from './setup';
import { extractSchema, compareSchemas, formatComparisonResult } from '../utils/schema-compare';

describe('Polygon Trades API', () => {
  const testDate = config.testStartDate;

  describe('GET /v3/trades/{symbol}', () => {
    describe('Required Parameters', () => {
      it.each(config.testSymbols)(
        'should return matching schema for %s trades',
        async (symbol) => {
          const polygonResponse = await polygonClient.getTrades(symbol, { limit: 10 });
          const simulatorResponse = await simulatorClient.getTrades(symbol, { limit: 10 });

          const polygonSchema = extractSchema(polygonResponse.data);
          const simulatorSchema = extractSchema(simulatorResponse.data);
          const comparison = compareSchemas(polygonSchema, simulatorSchema);

          logTestResult(
            `Trades ${symbol}`,
            polygonResponse.status,
            simulatorResponse.status,
            comparison.match
          );

          if (!comparison.match) {
            console.log(formatComparisonResult(comparison));
          }

          expect(polygonResponse.status).toBe(simulatorResponse.status);
          expect(comparison.match).toBe(true);
        }
      );
    });

    describe('Optional Parameters', () => {
      it('should handle timestamp.gte parameter', async () => {
        const symbol = config.testSymbols[0];
        const timestamp = `${testDate}T09:30:00Z`;

        const polygonResponse = await polygonClient.getTrades(symbol, {
          'timestamp.gte': timestamp,
          limit: 10,
        });
        const simulatorResponse = await simulatorClient.getTrades(symbol, {
          'timestamp.gte': timestamp,
          limit: 10,
        });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Trades timestamp.gte',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle timestamp.lte parameter', async () => {
        const symbol = config.testSymbols[0];
        const timestamp = `${testDate}T16:00:00Z`;

        const polygonResponse = await polygonClient.getTrades(symbol, {
          'timestamp.lte': timestamp,
          limit: 10,
        });
        const simulatorResponse = await simulatorClient.getTrades(symbol, {
          'timestamp.lte': timestamp,
          limit: 10,
        });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Trades timestamp.lte',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle order=asc parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getTrades(symbol, {
          order: 'asc',
          limit: 10,
        });
        const simulatorResponse = await simulatorClient.getTrades(symbol, {
          order: 'asc',
          limit: 10,
        });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Trades order=asc',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle order=desc parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getTrades(symbol, {
          order: 'desc',
          limit: 10,
        });
        const simulatorResponse = await simulatorClient.getTrades(symbol, {
          order: 'desc',
          limit: 10,
        });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Trades order=desc',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle limit parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getTrades(symbol, { limit: 5 });
        const simulatorResponse = await simulatorClient.getTrades(symbol, { limit: 5 });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Trades limit=5',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });
    });
  });

  describe('GET /v2/ticks/stocks/trades/{symbol}/{date}', () => {
    describe('Required Parameters', () => {
      it.each(config.testSymbols)(
        'should return matching schema for %s historic trades',
        async (symbol) => {
          const polygonResponse = await polygonClient.getHistoricTrades(symbol, testDate, {
            limit: 10,
          });
          const simulatorResponse = await simulatorClient.getHistoricTrades(symbol, testDate, {
            limit: 10,
          });

          const polygonSchema = extractSchema(polygonResponse.data);
          const simulatorSchema = extractSchema(simulatorResponse.data);
          const comparison = compareSchemas(polygonSchema, simulatorSchema);

          logTestResult(
            `Historic trades ${symbol}`,
            polygonResponse.status,
            simulatorResponse.status,
            comparison.match
          );

          if (!comparison.match) {
            console.log(formatComparisonResult(comparison));
          }

          expect(polygonResponse.status).toBe(simulatorResponse.status);
          expect(comparison.match).toBe(true);
        }
      );
    });

    describe('Optional Parameters', () => {
      it('should handle reverse=true parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getHistoricTrades(symbol, testDate, {
          reverse: true,
          limit: 10,
        });
        const simulatorResponse = await simulatorClient.getHistoricTrades(symbol, testDate, {
          reverse: true,
          limit: 10,
        });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Historic trades reverse=true',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle limit parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getHistoricTrades(symbol, testDate, {
          limit: 5,
        });
        const simulatorResponse = await simulatorClient.getHistoricTrades(symbol, testDate, {
          limit: 5,
        });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Historic trades limit=5',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });
    });
  });

  describe('GET /v2/last/trade/{symbol}', () => {
    describe('Required Parameters', () => {
      it.each(config.testSymbols)(
        'should return matching schema for %s last trade',
        async (symbol) => {
          const polygonResponse = await polygonClient.getLastTrade(symbol);
          const simulatorResponse = await simulatorClient.getLastTrade(symbol);

          const polygonSchema = extractSchema(polygonResponse.data);
          const simulatorSchema = extractSchema(simulatorResponse.data);
          const comparison = compareSchemas(polygonSchema, simulatorSchema);

          logTestResult(
            `Last trade ${symbol}`,
            polygonResponse.status,
            simulatorResponse.status,
            comparison.match
          );

          if (!comparison.match) {
            console.log(formatComparisonResult(comparison));
          }

          expect(polygonResponse.status).toBe(simulatorResponse.status);
          expect(comparison.match).toBe(true);
        }
      );
    });
  });
});
