#pragma once

#include <glm/glm.hpp>

//Colors
struct COL_RGB{
    COL_RGB() = default;
    COL_RGB(float r, float g, float b){
        components[0] = r;
        components[1] = g;
        components[2] = b;
    };

    COL_RGB(float scalar){
        components[0] = scalar;
        components[1] = scalar;
        components[2] = scalar;
    };

    COL_RGB(glm::vec3 col){
        components[0] = col.x;
        components[1] = col.y;
        components[2] = col.z;
    }

    COL_RGB& operator=(const COL_RGB& other) = default;

    COL_RGB operator+(const COL_RGB& other) const {
        return COL_RGB(
            components[0] + other.components[0],
            components[1] + other.components[1],
            components[2] + other.components[2]
        );
    }

    COL_RGB operator-(const COL_RGB& other) const {
        return COL_RGB(
            components[0] - other.components[0],
            components[1] - other.components[1],
            components[2] - other.components[2]
        );
    }

    COL_RGB operator*(const COL_RGB& other) const {
        return COL_RGB(
            components[0] * other.components[0],
            components[1] * other.components[1],
            components[2] * other.components[2]
        );
    }

    COL_RGB operator/(const COL_RGB& other) const {
        return COL_RGB(
            components[0] / other.components[0],
            components[1] / other.components[1],
            components[2] / other.components[2]
        );
    }

    COL_RGB operator+(float scalar) const {
        return COL_RGB(
            components[0] + scalar,
            components[1] + scalar,
            components[2] + scalar
        );
    }

    COL_RGB operator-(float scalar) const {
        return COL_RGB(
            components[0] - scalar,
            components[1] - scalar,
            components[2] - scalar
        );
    }

    COL_RGB operator*(float scalar) const {
        return COL_RGB(
            components[0] * scalar,
            components[1] * scalar,
            components[2] * scalar
        );
    }

    COL_RGB operator/(float scalar) const {
        return COL_RGB(
            components[0] / scalar,
            components[1] / scalar,
            components[2] / scalar
        );
    }

    bool operator==(const COL_RGB& other) const {
        const float eps = 1e-6f;
        return fabs(components[0] - other.components[0]) < eps &&
            fabs(components[1] - other.components[1]) < eps &&
            fabs(components[2] - other.components[2]) < eps;
    }

    bool operator!=(const COL_RGB& other) const {
        return components[0] != other.components[0] || components[1] != other.components[1] || components[2] != other.components[2];
    }

    COL_RGB& operator+=(const COL_RGB& other) {
        components[0] += other.components[0];
        components[1] += other.components[1];
        components[2] += other.components[2];
        return *this;
    }

    COL_RGB& operator-=(const COL_RGB& other) {
        components[0] -= other.components[0];
        components[1] -= other.components[1];
        components[2] -= other.components[2];
        return *this;
    }

    COL_RGB& operator*=(const COL_RGB& other) {
        components[0] *= other.components[0];
        components[1] *= other.components[1];
        components[2] *= other.components[2];
        return *this;
    }

    COL_RGB& operator/=(const COL_RGB& other) {
        components[0] /= other.components[0];
        components[1] /= other.components[1];
        components[2] /= other.components[2];
        return *this;
    }

    COL_RGB& operator+=(float scalar) {
        components[0] += scalar;
        components[1] += scalar;
        components[2] += scalar;
        return *this;
    }

    COL_RGB& operator-=(float scalar) {
        components[0] -= scalar;
        components[1] -= scalar;
        components[2] -= scalar;
        return *this;
    }

    COL_RGB& operator*=(float scalar) {
        components[0] *= scalar;
        components[1] *= scalar;
        components[2] *= scalar;
        return *this;
    }

    COL_RGB& operator/=(float scalar) {
        components[0] /= scalar;
        components[1] /= scalar;
        components[2] /= scalar;
        return *this;
    }

    operator glm::vec3() const {
        return glm::vec3(components[0], components[1], components[2]);
    }

    float& r() { return components[0]; }
    float& g() { return components[1]; }
    float& b() { return components[2]; }

    private:
        float components[3] = {0.0f, 0.0f, 0.0f};
};

struct COL_RGBA{    
    COL_RGBA() = default;

    COL_RGBA(glm::vec4 col){
        components[0] = col.x;
        components[1] = col.y;
        components[2] = col.z;
        components[3] = col.w;
    }

    COL_RGBA(glm::vec3 col){
        components[0] = col.x;
        components[1] = col.y;
        components[2] = col.z;
        components[3] = 1.0f;
    }

    COL_RGBA(float r, float g, float b, float a){
        components[0] = r;
        components[1] = g;
        components[2] = b;
        components[3] = a;
    };

    COL_RGBA(float r, float g, float b){
        components[0] = r;
        components[1] = g;
        components[2] = b;
        components[3] = 1.0f;
    };

    COL_RGBA(float scalar){
        components[0] = scalar;
        components[1] = scalar;
        components[2] = scalar;
        components[3] = scalar;
    };

    COL_RGBA& operator=(const COL_RGBA& other) = default;

    bool operator==(const COL_RGBA& other) const {
        return components[0] == other.components[0] && components[1] == other.components[1] && components[2] == other.components[2] && components[3] == other.components[3];
    }

    bool operator!=(const COL_RGBA& other) const {
        return components[0] != other.components[0] || components[1] != other.components[1] || components[2] != other.components[2] || components[3] != other.components[3];
    }

    COL_RGBA operator+(const COL_RGBA& other) const {
        return COL_RGBA(
            components[0] + other.components[0],
            components[1] + other.components[1],
            components[2] + other.components[2],
            components[3] + other.components[3]
        );
    }

    COL_RGBA operator-(const COL_RGBA& other) const {
        return COL_RGBA(
            components[0] - other.components[0],
            components[1] - other.components[1],
            components[2] - other.components[2],
            components[3] - other.components[3]
        );
    }

    COL_RGBA operator*(const COL_RGBA& other) const {
        return COL_RGBA(
            components[0] * other.components[0],
            components[1] * other.components[1],
            components[2] * other.components[2],
            components[3] * other.components[3]
        ); 
    }

    COL_RGBA operator/(const COL_RGBA& other) const {
        return COL_RGBA(
            components[0] / other.components[0],
            components[1] / other.components[1],
            components[2] / other.components[2],
            components[3] / other.components[3]
        );
    }

    COL_RGBA operator+(float scalar) const {
        return COL_RGBA(
            components[0] + scalar,
            components[1] + scalar,
            components[2] + scalar,
            components[3] + scalar
        );
    }

    COL_RGBA operator-(float scalar) const {
        return COL_RGBA(
            components[0] - scalar,
            components[1] - scalar,
            components[2] - scalar,
            components[3] - scalar
        );
    }

    COL_RGBA operator*(float scalar) const {
        return COL_RGBA(
            components[0] * scalar,
            components[1] * scalar,
            components[2] * scalar,
            components[3] * scalar
        );
    }

    COL_RGBA operator/(float scalar) const {
        return COL_RGBA(
            components[0] / scalar,
            components[1] / scalar,
            components[2] / scalar,
            components[3] / scalar
        );
    }

    COL_RGBA& operator+=(const COL_RGBA& other) {
        components[0] += other.components[0];
        components[1] += other.components[1];
        components[2] += other.components[2];
        components[3] += other.components[3];
        return *this;
    }

    COL_RGBA& operator-=(const COL_RGBA& other) {
        components[0] -= other.components[0];
        components[1] -= other.components[1];
        components[2] -= other.components[2];
        components[3] -= other.components[3];
        return *this;
    }

    COL_RGBA& operator*=(const COL_RGBA& other) {
        components[0] *= other.components[0];
        components[1] *= other.components[1];
        components[2] *= other.components[2];
        components[3] *= other.components[3];
        return *this;
    }

    COL_RGBA& operator/=(const COL_RGBA& other) {
        components[0] /= other.components[0];
        components[1] /= other.components[1];
        components[2] /= other.components[2];
        components[3] /= other.components[3];
        return *this;
    }

    COL_RGBA& operator+=(float scalar) {
        components[0] += scalar;
        components[1] += scalar;
        components[2] += scalar;
        components[3] += scalar;
        return *this;
    }

    COL_RGBA& operator-=(float scalar) {
        components[0] -= scalar;
        components[1] -= scalar;
        components[2] -= scalar;
        components[3] -= scalar;
        return *this;
    }

    COL_RGBA& operator*=(float scalar) {
        components[0] *= scalar;
        components[1] *= scalar;
        components[2] *= scalar;
        components[3] *= scalar;
        return *this;
    }

    COL_RGBA& operator/=(float scalar) {
        components[0] /= scalar;
        components[1] /= scalar;
        components[2] /= scalar;
        components[3] /= scalar;
        return *this;
    }

    operator glm::vec4() const {
        return glm::vec4(components[0], components[1], components[2], components[3]);
    }

    operator glm::vec3() const {
        return glm::vec3(components[0], components[1], components[2]);
    }

    operator COL_RGB() const {
        return COL_RGB(components[0], components[1], components[2]);
    }

    COL_RGB rgb() const {
        return COL_RGB(components[0], components[1], components[2]);
    }

    float& r() { return components[0]; }
    float& g() { return components[1]; }
    float& b() { return components[2]; }
    float& a() { return components[3]; }

    private:

        float components[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};