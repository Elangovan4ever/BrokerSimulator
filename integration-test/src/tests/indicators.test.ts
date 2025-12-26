/**
 * Integration tests for Polygon Technical Indicators API
 * Tests: SMA, EMA, RSI, MACD
 */

import { polygonClient, simulatorClient, config, logTestResult } from './setup';
import { extractSchema, compareSchemas, formatComparisonResult } from '../utils/schema-compare';

describe('Polygon Technical Indicators API', () => {
  describe('GET /v1/indicators/sma/{symbol}', () => {
    describe('Required Parameters', () => {
      it.each(config.testSymbols)(
        'should return matching schema for %s SMA',
        async (symbol) => {
          const polygonResponse = await polygonClient.getSMA(symbol, { limit: 10 });
          const simulatorResponse = await simulatorClient.getSMA(symbol, { limit: 10 });

          const polygonSchema = extractSchema(polygonResponse.data);
          const simulatorSchema = extractSchema(simulatorResponse.data);
          const comparison = compareSchemas(polygonSchema, simulatorSchema);

          logTestResult(
            `SMA ${symbol}`,
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
      it('should handle window parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getSMA(symbol, { window: 20, limit: 10 });
        const simulatorResponse = await simulatorClient.getSMA(symbol, { window: 20, limit: 10 });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'SMA window=20',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle timespan parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getSMA(symbol, { timespan: 'day', limit: 10 });
        const simulatorResponse = await simulatorClient.getSMA(symbol, { timespan: 'day', limit: 10 });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'SMA timespan=day',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle series_type parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getSMA(symbol, { series_type: 'close', limit: 10 });
        const simulatorResponse = await simulatorClient.getSMA(symbol, { series_type: 'close', limit: 10 });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'SMA series_type=close',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle order parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getSMA(symbol, { order: 'desc', limit: 10 });
        const simulatorResponse = await simulatorClient.getSMA(symbol, { order: 'desc', limit: 10 });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'SMA order=desc',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });
    });
  });

  describe('GET /v1/indicators/ema/{symbol}', () => {
    describe('Required Parameters', () => {
      it.each(config.testSymbols)(
        'should return matching schema for %s EMA',
        async (symbol) => {
          const polygonResponse = await polygonClient.getEMA(symbol, { limit: 10 });
          const simulatorResponse = await simulatorClient.getEMA(symbol, { limit: 10 });

          const polygonSchema = extractSchema(polygonResponse.data);
          const simulatorSchema = extractSchema(simulatorResponse.data);
          const comparison = compareSchemas(polygonSchema, simulatorSchema);

          logTestResult(
            `EMA ${symbol}`,
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
      it('should handle window parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getEMA(symbol, { window: 12, limit: 10 });
        const simulatorResponse = await simulatorClient.getEMA(symbol, { window: 12, limit: 10 });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'EMA window=12',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle timespan=hour parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getEMA(symbol, { timespan: 'hour', limit: 10 });
        const simulatorResponse = await simulatorClient.getEMA(symbol, { timespan: 'hour', limit: 10 });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'EMA timespan=hour',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });
    });
  });

  describe('GET /v1/indicators/rsi/{symbol}', () => {
    describe('Required Parameters', () => {
      it.each(config.testSymbols)(
        'should return matching schema for %s RSI',
        async (symbol) => {
          const polygonResponse = await polygonClient.getRSI(symbol, { limit: 10 });
          const simulatorResponse = await simulatorClient.getRSI(symbol, { limit: 10 });

          const polygonSchema = extractSchema(polygonResponse.data);
          const simulatorSchema = extractSchema(simulatorResponse.data);
          const comparison = compareSchemas(polygonSchema, simulatorSchema);

          logTestResult(
            `RSI ${symbol}`,
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
      it('should handle window=14 parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getRSI(symbol, { window: 14, limit: 10 });
        const simulatorResponse = await simulatorClient.getRSI(symbol, { window: 14, limit: 10 });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'RSI window=14',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle timespan=day parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getRSI(symbol, { timespan: 'day', limit: 10 });
        const simulatorResponse = await simulatorClient.getRSI(symbol, { timespan: 'day', limit: 10 });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'RSI timespan=day',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });
    });
  });

  describe('GET /v1/indicators/macd/{symbol}', () => {
    describe('Required Parameters', () => {
      it.each(config.testSymbols)(
        'should return matching schema for %s MACD',
        async (symbol) => {
          const polygonResponse = await polygonClient.getMACD(symbol, { limit: 10 });
          const simulatorResponse = await simulatorClient.getMACD(symbol, { limit: 10 });

          const polygonSchema = extractSchema(polygonResponse.data);
          const simulatorSchema = extractSchema(simulatorResponse.data);
          const comparison = compareSchemas(polygonSchema, simulatorSchema);

          logTestResult(
            `MACD ${symbol}`,
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
      it('should handle short_window parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getMACD(symbol, { short_window: 12, limit: 10 });
        const simulatorResponse = await simulatorClient.getMACD(symbol, { short_window: 12, limit: 10 });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'MACD short_window=12',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle long_window parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getMACD(symbol, { long_window: 26, limit: 10 });
        const simulatorResponse = await simulatorClient.getMACD(symbol, { long_window: 26, limit: 10 });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'MACD long_window=26',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle signal_window parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getMACD(symbol, { signal_window: 9, limit: 10 });
        const simulatorResponse = await simulatorClient.getMACD(symbol, { signal_window: 9, limit: 10 });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'MACD signal_window=9',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle timespan=day parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getMACD(symbol, { timespan: 'day', limit: 10 });
        const simulatorResponse = await simulatorClient.getMACD(symbol, { timespan: 'day', limit: 10 });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'MACD timespan=day',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });
    });
  });
});
