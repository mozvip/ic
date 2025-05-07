#include "progress_indicator.h"
#include <math.h>
#include <stdlib.h> // For malloc, free
#include <SDL3/SDL_log.h>

SDL_FColor white_f = {1.0f, 1.0f, 1.0f, 1.0f}; // White as SDL_FColor

// Ensure M_PI and M_PI_2 are defined
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#ifndef M_PI_2
#define M_PI_2 (M_PI / 2.0f)
#endif

void draw_progress_indicator(SDL_Renderer *renderer, float progress, int centerX, int centerY, int radius) {
    // Draw a filled circle using triangles like a pie chart
    int segments = 36; // Number of segments for a full circle, adjust for more/less smoothness
    float angle_step = 2.0f * M_PI / segments;

    // Calculate filled segments based on progress
    int filledSegments = (int)(progress * segments);
    if (progress > 0.0f && filledSegments < 1) { // If there's any progress, draw at least one segment
        filledSegments = 1;
    }
    if (filledSegments > segments) { // Cap at full circle
        filledSegments = segments;
    }

    if (filledSegments <= 0) { // No progress or invalid progress, nothing to draw
        return;
    }

    // Starting angle: -90 degrees (top of the circle) to draw clockwise
    float startAngle = -M_PI_2;

    // Each segment is a triangle, so 3 vertices per segment
    SDL_Vertex *vertices = (SDL_Vertex *)malloc(filledSegments * 3 * sizeof(SDL_Vertex));
    if (!vertices) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate memory for progress indicator vertices");
        return; // Allocation failed
    }

    for (int i = 0; i < filledSegments; i++) {
        float angle1 = startAngle + (i * angle_step);
        float angle2 = startAngle + ((i + 1) * angle_step);

        // Points on the circumference
        float x1 = centerX + cosf(angle1) * radius;
        float y1 = centerY + sinf(angle1) * radius;
        float x2 = centerX + cosf(angle2) * radius;
        float y2 = centerY + sinf(angle2) * radius;

        // Vertex 0: Center of the circle for the current triangle
        vertices[i * 3 + 0].position.x = (float)centerX;
        vertices[i * 3 + 0].position.y = (float)centerY;
        vertices[i * 3 + 0].color = white_f;
        vertices[i * 3 + 0].tex_coord.x = 0.0f; // Not using texture
        vertices[i * 3 + 0].tex_coord.y = 0.0f;

        // Vertex 1: Point on the circumference (start of segment arc)
        vertices[i * 3 + 1].position.x = x1;
        vertices[i * 3 + 1].position.y = y1;
        vertices[i * 3 + 1].color = white_f;
        vertices[i * 3 + 1].tex_coord.x = 0.0f;
        vertices[i * 3 + 1].tex_coord.y = 0.0f;

        // Vertex 2: Point on the circumference (end of segment arc)
        vertices[i * 3 + 2].position.x = x2;
        vertices[i * 3 + 2].position.y = y2;
        vertices[i * 3 + 2].color = white_f;
        vertices[i * 3 + 2].tex_coord.x = 0.0f;
        vertices[i * 3 + 2].tex_coord.y = 0.0f;
    }

    // Render all triangle segments
    SDL_RenderGeometry(renderer, NULL, vertices, filledSegments * 3, NULL, 0);

    free(vertices); // Free the allocated memory
}
