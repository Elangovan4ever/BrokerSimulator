/**
 * Integration tests for Polygon IPOs API
 * Tests: /vX/reference/ipos
 */

import { polygonClient, simulatorClient, logTestResult } from './setup';
import { extractSchema, compareSchemas, formatComparisonResult } from '../utils/schema-compare';

describe('Polygon IPOs API', () => {
  describe('GET /vX/reference/ipos', () => {
    it('should return matching schema for IPOs list', async () => {
      const polygonResponse = await polygonClient.getIpos({ limit: 10 });
      const simulatorResponse = await simulatorClient.getIpos({ limit: 10 });

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      logTestResult(
        'IPOs list',
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

    it('should handle ipo_status=history parameter', async () => {
      const polygonResponse = await polygonClient.getIpos({ ipo_status: 'history', limit: 10 });
      const simulatorResponse = await simulatorClient.getIpos({ ipo_status: 'history', limit: 10 });

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      logTestResult(
        'IPOs ipo_status=history',
        polygonResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      expect(polygonResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
    });

    it('should handle announced_date.gte parameter', async () => {
      const polygonResponse = await polygonClient.getIpos({
        'announced_date.gte': '2015-01-01',
        limit: 10,
      });
      const simulatorResponse = await simulatorClient.getIpos({
        'announced_date.gte': '2015-01-01',
        limit: 10,
      });

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      logTestResult(
        'IPOs announced_date.gte',
        polygonResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      expect(polygonResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
    });

    it('should match pagination next_url presence', async () => {
      const polygonResponse = await polygonClient.getIpos({ limit: 1 });
      const simulatorResponse = await simulatorClient.getIpos({ limit: 1 });

      const polygonSchema = extractSchema(polygonResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(polygonSchema, simulatorSchema);

      const polygonHasNext = Boolean(polygonResponse.data?.next_url);
      const simulatorHasNext = Boolean(simulatorResponse.data?.next_url);

      logTestResult(
        'IPOs pagination next_url',
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
