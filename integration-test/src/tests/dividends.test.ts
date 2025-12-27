/**
 * Integration tests for Polygon Dividends API
 * Tests: /v3/reference/dividends
 */

import { polygonClient, simulatorClient, config, getSimDate, expectResultsNotEmpty, logTestResult } from './setup';
import { extractSchema, compareSchemas, formatComparisonResult } from '../utils/schema-compare';

describe('Polygon Dividends API', () => {
  describe('GET /v3/reference/dividends', () => {
    const dividendSymbols = config.testSymbols.filter((symbol) => symbol !== 'AMZN');

    describe('Required Parameters', () => {
      it.each(dividendSymbols)(
        'should return matching schema for %s dividends',
        async (symbol) => {
          const cutoffDate = getSimDate();
          const polygonResponse = await polygonClient.getDividends({
            ticker: symbol,
            'ex_dividend_date.lte': cutoffDate,
            limit: 10,
          });
          const simulatorResponse = await simulatorClient.getDividends({
            ticker: symbol,
            'ex_dividend_date.lte': cutoffDate,
            limit: 10,
          });

          expectResultsNotEmpty(`Dividends ${symbol} polygon`, polygonResponse.data);
          expectResultsNotEmpty(`Dividends ${symbol} simulator`, simulatorResponse.data);

          const polygonSchema = extractSchema(polygonResponse.data);
          const simulatorSchema = extractSchema(simulatorResponse.data);
          const comparison = compareSchemas(polygonSchema, simulatorSchema);

          logTestResult(
            `Dividends ${symbol}`,
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

      it('should return empty results for a non-dividend symbol', async () => {
        const cutoffDate = getSimDate();
        const polygonResponse = await polygonClient.getDividends({
          ticker: 'AMZN',
          'ex_dividend_date.lte': cutoffDate,
          limit: 10,
        });
        const simulatorResponse = await simulatorClient.getDividends({
          ticker: 'AMZN',
          'ex_dividend_date.lte': cutoffDate,
          limit: 10,
        });

        const polygonResults = polygonResponse.data?.results || [];
        const simulatorResults = simulatorResponse.data?.results || [];

        logTestResult(
          'Dividends AMZN empty',
          polygonResponse.status,
          simulatorResponse.status,
          true
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(polygonResults.length).toBe(0);
        expect(simulatorResults.length).toBe(0);
      });
    });

    describe('Optional Parameters', () => {
      it('should handle ex_dividend_date.gte parameter', async () => {
        const symbol = config.testSymbols[0];
        const cutoffDate = getSimDate();

        const polygonResponse = await polygonClient.getDividends({
          ticker: symbol,
          'ex_dividend_date.gte': '2024-01-01',
          'ex_dividend_date.lte': cutoffDate,
          limit: 10,
        });
        const simulatorResponse = await simulatorClient.getDividends({
          ticker: symbol,
          'ex_dividend_date.gte': '2024-01-01',
          'ex_dividend_date.lte': cutoffDate,
          limit: 10,
        });

        expectResultsNotEmpty('Dividends ex_dividend_date.gte polygon', polygonResponse.data);
        expectResultsNotEmpty('Dividends ex_dividend_date.gte simulator', simulatorResponse.data);

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Dividends ex_dividend_date.gte',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle ex_dividend_date.lte parameter', async () => {
        const symbol = config.testSymbols[0];
        const cutoffDate = getSimDate();

        const polygonResponse = await polygonClient.getDividends({
          ticker: symbol,
          'ex_dividend_date.lte': cutoffDate,
          limit: 10,
        });
        const simulatorResponse = await simulatorClient.getDividends({
          ticker: symbol,
          'ex_dividend_date.lte': cutoffDate,
          limit: 10,
        });

        expectResultsNotEmpty('Dividends ex_dividend_date.lte polygon', polygonResponse.data);
        expectResultsNotEmpty('Dividends ex_dividend_date.lte simulator', simulatorResponse.data);

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Dividends ex_dividend_date.lte',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle order=asc parameter', async () => {
        const symbol = config.testSymbols[0];
        const cutoffDate = getSimDate();

        const polygonResponse = await polygonClient.getDividends({
          ticker: symbol,
          'ex_dividend_date.lte': cutoffDate,
          order: 'asc',
          limit: 10,
        });
        const simulatorResponse = await simulatorClient.getDividends({
          ticker: symbol,
          'ex_dividend_date.lte': cutoffDate,
          order: 'asc',
          limit: 10,
        });

        expectResultsNotEmpty('Dividends order=asc polygon', polygonResponse.data);
        expectResultsNotEmpty('Dividends order=asc simulator', simulatorResponse.data);

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Dividends order=asc',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle order=desc parameter', async () => {
        const symbol = config.testSymbols[0];
        const cutoffDate = getSimDate();

        const polygonResponse = await polygonClient.getDividends({
          ticker: symbol,
          'ex_dividend_date.lte': cutoffDate,
          order: 'desc',
          limit: 10,
        });
        const simulatorResponse = await simulatorClient.getDividends({
          ticker: symbol,
          'ex_dividend_date.lte': cutoffDate,
          order: 'desc',
          limit: 10,
        });

        expectResultsNotEmpty('Dividends order=desc polygon', polygonResponse.data);
        expectResultsNotEmpty('Dividends order=desc simulator', simulatorResponse.data);

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Dividends order=desc',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle limit parameter', async () => {
        const symbol = config.testSymbols[0];
        const cutoffDate = getSimDate();

        const polygonResponse = await polygonClient.getDividends({
          ticker: symbol,
          'ex_dividend_date.lte': cutoffDate,
          limit: 5,
        });
        const simulatorResponse = await simulatorClient.getDividends({
          ticker: symbol,
          'ex_dividend_date.lte': cutoffDate,
          limit: 5,
        });

        expectResultsNotEmpty('Dividends limit=5 polygon', polygonResponse.data);
        expectResultsNotEmpty('Dividends limit=5 simulator', simulatorResponse.data);

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Dividends limit=5',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle dividend_type parameter', async () => {
        const symbol = config.testSymbols[0];
        const cutoffDate = getSimDate();

        const polygonResponse = await polygonClient.getDividends({
          ticker: symbol,
          dividend_type: 'CD',
          'ex_dividend_date.lte': cutoffDate,
          limit: 10,
        });
        const simulatorResponse = await simulatorClient.getDividends({
          ticker: symbol,
          dividend_type: 'CD',
          'ex_dividend_date.lte': cutoffDate,
          limit: 10,
        });

        expectResultsNotEmpty('Dividends dividend_type=CD polygon', polygonResponse.data);
        expectResultsNotEmpty('Dividends dividend_type=CD simulator', simulatorResponse.data);

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Dividends dividend_type=CD',
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
