#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec3 vertexTangent;
in vec3 vertexBitangent;

out vec2 fragTexCoord;
out vec3 fragNormal;
out vec3 fragPosition;
out vec3 fragTangent;
out vec3 fragBitangent;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;

void main() {
    fragTexCoord = vertexTexCoord;
    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));

    // Переводим нормали и тангенты в мировое пространство
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    fragTangent = normalize(vec3(matNormal * vec4(vertexTangent.xyz, 0.0)));

    // Бинормаль вычисляем через векторное произведение, чтобы TBN была строго ортонормированной
    fragBitangent = cross(fragNormal, fragTangent);

    gl_Position = mvp * vec4(vertexPosition, 1.0);
}