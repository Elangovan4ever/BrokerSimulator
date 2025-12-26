/**
 * Integration tests for Polygon News API
 * Tests: /v2/reference/news
 */

import { polygonClient, simulatorClient, logTestResult } from './setup';
import { extractSchema, compareSchemas, formatComparisonResult } from '../utils/schema-compare';

describe('Polygon News API', () => {
  describe('GET /v2/reference/news', () => {
    it('should return matching schema for AAPL news', async () => {
      const polygonResponse = await polygonClient.getNews({ ticker: 'AAPL', limit: 10 });
      const simulatorResponse = await simulatorClient.getNews({ ticker: 'AAPL', limit: 10 });

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      logTestResult(
        'News AAPL',
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

    it('should handle published_utc.gte parameter', async () => {
      const polygonResponse = await polygonClient.getNews({
        ticker: 'AAPL',
        'published_utc.gte': '2024-01-01',
        limit: 10,
      });
      const simulatorResponse = await simulatorClient.getNews({
        ticker: 'AAPL',
        'published_utc.gte': '2024-01-01',
        limit: 10,
      });

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      logTestResult(
        'News published_utc.gte',
        polygonResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      expect(polygonResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
    });

    it('should handle order=ascending parameter', async () => {
      const polygonResponse = await polygonClient.getNews({
        ticker: 'AAPL',
        order: 'ascending',
        limit: 10,
      });
      const simulatorResponse = await simulatorClient.getNews({
        ticker: 'AAPL',
        order: 'ascending',
        limit: 10,
      });

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      logTestResult(
        'News order=ascending',
        polygonResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      expect(polygonResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
    });

    it('should handle include_insights=false parameter', async () => {
      const polygonResponse = await polygonClient.getNews({
        ticker: 'AAPL',
        include_insights: false,
        limit: 10,
      });
      const simulatorResponse = await simulatorClient.getNews({
        ticker: 'AAPL',
        include_insights: false,
        limit: 10,
      });

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      logTestResult(
        'News include_insights=false',
        polygonResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      expect(polygonResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
    });

    it('should match pagination next_url presence', async () => {
      const polygonResponse = await polygonClient.getNews({ ticker: 'AAPL', limit: 1 });
      const simulatorResponse = await simulatorClient.getNews({ ticker: 'AAPL', limit: 1 });

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      const polygonHasNext = Boolean(polygonResponse.data?.next_url);
      const simulatorHasNext = Boolean(simulatorResponse.data?.next_url);

      logTestResult(
        'News pagination next_url',
        polygonResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      expect(polygonResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
      expect(polygonHasNext).toBe(simulatorHasNext);
    });
  });
});
