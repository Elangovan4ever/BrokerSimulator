/**
 * Integration tests for Polygon Ticker Details API
 * Tests: /v3/reference/tickers/{symbol}, /v3/reference/tickers
 */

import { polygonClient, simulatorClient, config, logTestResult } from './setup';
import { extractSchema, compareSchemas, formatComparisonResult } from '../utils/schema-compare';

describe('Polygon Tickers API', () => {
  describe('GET /v3/reference/tickers/{symbol}', () => {
    describe('Required Parameters', () => {
      it.each(config.testSymbols)(
        'should return matching schema for %s ticker details',
        async (symbol) => {
          const polygonResponse = await polygonClient.getTickerDetails(symbol);
          const simulatorResponse = await simulatorClient.getTickerDetails(symbol);

          const polygonSchema = extractSchema(polygonResponse.data);
          const simulatorSchema = extractSchema(simulatorResponse.data);
          const comparison = compareSchemas(polygonSchema, simulatorSchema);

          logTestResult(
            `Ticker details ${symbol}`,
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
      it('should handle date parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getTickerDetails(symbol, {
          date: config.testStartDate,
        });
        const simulatorResponse = await simulatorClient.getTickerDetails(symbol, {
          date: config.testStartDate,
        });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Ticker details with date',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });
    });
  });

  describe('GET /v3/reference/tickers', () => {
    describe('Required Parameters', () => {
      it('should return matching schema for list tickers', async () => {
        const polygonResponse = await polygonClient.listTickers({ limit: 10 });
        const simulatorResponse = await simulatorClient.listTickers({ limit: 10 });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'List tickers',
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

    describe('Optional Parameters', () => {
      it('should handle ticker filter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.listTickers({ ticker: symbol });
        const simulatorResponse = await simulatorClient.listTickers({ ticker: symbol });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'List tickers with ticker filter',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle type parameter', async () => {
        const polygonResponse = await polygonClient.listTickers({ type: 'CS', limit: 10 });
        const simulatorResponse = await simulatorClient.listTickers({ type: 'CS', limit: 10 });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'List tickers type=CS',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle market parameter', async () => {
        const polygonResponse = await polygonClient.listTickers({ market: 'stocks', limit: 10 });
        const simulatorResponse = await simulatorClient.listTickers({ market: 'stocks', limit: 10 });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'List tickers market=stocks',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle active=true parameter', async () => {
        const polygonResponse = await polygonClient.listTickers({ active: true, limit: 10 });
        const simulatorResponse = await simulatorClient.listTickers({ active: true, limit: 10 });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'List tickers active=true',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle search parameter', async () => {
        const polygonResponse = await polygonClient.listTickers({ search: 'Apple', limit: 10 });
        const simulatorResponse = await simulatorClient.listTickers({ search: 'Apple', limit: 10 });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'List tickers search=Apple',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle order=asc parameter', async () => {
        const polygonResponse = await polygonClient.listTickers({ order: 'asc', limit: 10 });
        const simulatorResponse = await simulatorClient.listTickers({ order: 'asc', limit: 10 });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'List tickers order=asc',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle sort parameter', async () => {
        const polygonResponse = await polygonClient.listTickers({ sort: 'ticker', limit: 10 });
        const simulatorResponse = await simulatorClient.listTickers({ sort: 'ticker', limit: 10 });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'List tickers sort=ticker',
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
